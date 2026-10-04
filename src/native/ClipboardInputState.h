// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Clipboard::Input
{
    // These values are part of the native/Papyrus protocol.
    enum class State : std::uint32_t
    {
        Unknown = 0, Queued = 1, Ready = 2, Accepted = 3,
        Cancelled = 4, Failed = 5, Interrupted = 6
    };

    enum class Reason : std::uint32_t
    {
        None, Busy, InvalidArguments, Unavailable, OpenTimeout, CloseTimeout,
        InvalidValue, UserCancel, OwnerCancelled, UnexpectedClose, LoadReset,
        DispatchRejected, ProtocolMismatch, TokenExhausted, CallerAbandoned
    };

    inline constexpr bool IsTerminal(State state) noexcept
    {
        return state == State::Accepted || state == State::Cancelled ||
            state == State::Failed || state == State::Interrupted;
    }

    struct Request
    {
        std::uint32_t owner{};
        std::string header;
        std::string defaultValue;
        std::string acceptLabel;
        std::string cancelLabel;
        std::string invalidLabel;
        std::int32_t inputType{ 2 };  // 0: unsigned integer; 1: positive decimal (6 places); 2: text.
        std::uint32_t maxChars{ 50 }; // UTF-16 code units, matching ActionScript String.length.
        std::uint32_t minimum{ 1 };
        std::uint32_t maximum{ 9999 };
    };

    struct Validation
    {
        bool valid{};
        bool empty{};
        std::uint32_t utf16Units{};
        std::uint32_t number{};
        std::uint64_t decimalMicros{};
    };

    // Validate the whole submitted string independently of the movie's input
    // filter. Reject malformed/overlong UTF-8, surrogates, control characters,
    // line separators and integer overflow. Ordinary Unicode and punctuation
    // remain data; final pattern-file validation is a separate boundary.
    inline Validation Validate(const Request& request, std::string_view value) noexcept
    {
        Validation result;
        result.empty = value.empty();
        if (request.maxChars == 0 || request.maxChars > 4096 ||
            (request.inputType < 0 || request.inputType > 2) ||
            (request.inputType != 2 && request.minimum > request.maximum)) {
            return result;
        }
        if (value.empty()) {
            result.valid = true;
            return result;
        }
        std::uint32_t number = 0;
        std::uint32_t fraction = 0, fractionalDigits = 0;
        bool decimalPoint = false, hasDigit = false;
        std::size_t index = 0;
        while (index < value.size()) {
            const auto first = static_cast<unsigned char>(value[index++]);
            std::uint32_t codepoint = first;
            std::uint32_t minimum = 0;
            unsigned remaining = 0;
            if (first < 0x80) {
                // ASCII needs no continuation bytes.
            } else if (first >= 0xC2 && first <= 0xDF) {
                codepoint = first & 0x1Fu;
                minimum = 0x80;
                remaining = 1;
            } else if (first >= 0xE0 && first <= 0xEF) {
                codepoint = first & 0x0Fu;
                minimum = 0x800;
                remaining = 2;
            } else if (first >= 0xF0 && first <= 0xF4) {
                codepoint = first & 0x07u;
                minimum = 0x10000;
                remaining = 3;
            } else {
                return result;
            }
            if (remaining > value.size() - index) { return result; }
            while (remaining-- > 0) {
                const auto next = static_cast<unsigned char>(value[index++]);
                if ((next & 0xC0u) != 0x80u) { return result; }
                codepoint = (codepoint << 6) | (next & 0x3Fu);
            }
            if (codepoint < minimum || codepoint > 0x10FFFF ||
                (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
                codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F) ||
                codepoint == 0x2028 || codepoint == 0x2029) {
                return result;
            }
            result.utf16Units += codepoint > 0xFFFF ? 2u : 1u;
            if (result.utf16Units > request.maxChars) { return result; }
            if (request.inputType != 2) {
                if (request.inputType == 1 && codepoint == '.' && !decimalPoint) {
                    decimalPoint = true;
                    continue;
                }
                if (codepoint < '0' || codepoint > '9') { return result; }
                const auto digit = codepoint - '0';
                hasDigit = true;
                if (decimalPoint) {
                    if (++fractionalDigits > 6) { return result; }
                    fraction = fraction * 10 + digit;
                    continue;
                }
                if (number > ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10) {
                    return result;
                }
                number = number * 10 + digit;
            }
        }
        result.number = number;
        if (request.inputType == 1) {
            if (!hasDigit || (decimalPoint && !fractionalDigits)) { return result; }
            for (; fractionalDigits < 6; ++fractionalDigits) { fraction *= 10; }
            // Compare decimal bounds exactly before converting to Papyrus float.
            // Zero remains cancellation-only; never round a seventh place into range.
            const auto scaled = static_cast<std::uint64_t>(number) * 1000000 + fraction;
            result.decimalMicros = scaled;
            result.valid = scaled > 0 &&
                scaled >= static_cast<std::uint64_t>(request.minimum) * 1000000 &&
                scaled <= static_cast<std::uint64_t>(request.maximum) * 1000000;
            return result;
        }
        result.valid = request.inputType != 0 ||
            (number >= request.minimum && number <= request.maximum);
        return result;
    }

    struct BeginResult
    {
        std::string token;
        Reason reason{ Reason::None };
        explicit operator bool() const noexcept { return !token.empty(); }
    };

    enum class Submission
    {
        Rejected, InvalidValue, Committed, AlreadyCommitted
    };

    struct Snapshot
    {
        std::string token;
        Request request;
        State state{ State::Unknown };
        Reason reason{ Reason::None };
        std::string value;
        bool opening{};
        bool ready{};
        bool released{ true };
        bool closeRequested{};
        bool closeExpired{};
        bool consumed{};
        bool deliveryInterrupted{};
        Reason deliveryReason{ Reason::None };
        std::uint64_t revision{};

        bool Terminal() const noexcept { return IsTerminal(state); }
        bool Finished() const noexcept { return Terminal() && (released || closeExpired); }
    };

    // Outcome and safe delivery are different contracts. Preserve the first
    // recorded answer for retry acknowledgement, but never apply it while UI
    // ownership is stuck or after its initiating operation was invalidated.
    inline State DeliveryState(const Snapshot& snapshot) noexcept
    {
        if (snapshot.deliveryInterrupted) { return State::Interrupted; }
        if (snapshot.closeExpired) { return State::Failed; }
        return snapshot.state;
    }

    inline std::string DeliverableValue(const Snapshot& snapshot)
    {
        return snapshot.state == State::Accepted && snapshot.released &&
            !snapshot.closeExpired && !snapshot.deliveryInterrupted ? snapshot.value : std::string{};
    }

    // Result retrieval and consumption are separate VM calls. The owner can be
    // invalidated in between, so a previously copied string is not sufficient
    // approval. Recheck this under the service lock immediately before Consume.
    inline bool ApprovalAtConsumption(const Snapshot& snapshot) noexcept
    {
        return DeliveryState(snapshot) == State::Accepted && snapshot.released && !snapshot.consumed;
    }

    // Engine-independent request coordinator. The service must serialize access
    // externally and must release its lock before engine operations or events.
    // Only one record is retained. Notifications carry no ownership: Read is
    // authoritative even when a ready/terminal notification never arrives.
    class Coordinator
    {
    public:
        using Milliseconds = std::int64_t;
        static constexpr Milliseconds kOpenTimeoutMs = 10000;
        static constexpr Milliseconds kCloseTimeoutMs = 5000;

        explicit Coordinator(Milliseconds openTimeout = kOpenTimeoutMs,
            Milliseconds closeTimeout = kCloseTimeoutMs) noexcept :
            _openTimeout(openTimeout > 0 ? openTimeout : kOpenTimeoutMs),
            _closeTimeout(closeTimeout > 0 ? closeTimeout : kCloseTimeoutMs)
        {}

        BeginResult Begin(Request request, Milliseconds now)
        {
            if (_record || !_orphanToken.empty()) { return { {}, Reason::Busy }; }
            if (request.maxChars == 0 || request.maxChars > 4096 ||
                (request.inputType < 0 || request.inputType > 2) ||
                (request.inputType != 2 && request.minimum > request.maximum)) {
                return { {}, Reason::InvalidArguments };
            }
            if (_exhausted || _sequence == (std::numeric_limits<std::uint64_t>::max)()) {
                return { {}, Reason::TokenExhausted };
            }
            ++_sequence;
            Snapshot snapshot;
            snapshot.token = std::to_string(_epoch) + ":" + std::to_string(_sequence);
            snapshot.request = std::move(request);
            snapshot.state = State::Queued;
            snapshot.revision = 1;
            _record.emplace(Record{ std::move(snapshot), now, now });
            return { _record->snapshot.token, Reason::None };
        }

        Snapshot Read(std::string_view token) const
        {
            return Matches(token) ? _record->snapshot : Snapshot{};
        }

        // The UI task claims the opening lease immediately before calling the
        // engine. Cancellation before this point needs no close; a late queued
        // task fails here and cannot open a cancelled request's menu.
        bool MarkOpening(std::string_view token)
        {
            if (!Matches(token) || _record->snapshot.state != State::Queued ||
                _record->snapshot.opening) { return false; }
            auto& snapshot = _record->snapshot;
            snapshot.opening = true;
            snapshot.released = false;
            ++snapshot.revision;
            return true;
        }

        bool MarkReady(std::string_view token, Milliseconds now)
        {
            Tick(now);
            if (Matches(token) && _record->snapshot.state == State::Ready) { return true; }
            if (!Matches(token) || _record->snapshot.state != State::Queued ||
                !_record->snapshot.opening) { return false; }
            auto& snapshot = _record->snapshot;
            snapshot.state = State::Ready;
            snapshot.ready = true;
            ++snapshot.revision;
            return true;
        }

        Submission Submit(std::string_view token, std::string_view value, Milliseconds now)
        {
            Tick(now);
            if (!Matches(token)) { return Submission::Rejected; }
            auto& snapshot = _record->snapshot;
            if (snapshot.Terminal()) {
                // ACK retry delivery without replacing an immutable result.
                return Submission::AlreadyCommitted;
            }
            if (snapshot.state != State::Ready) { return Submission::Rejected; }
            const auto validation = Validate(snapshot.request, value);
            if (!validation.valid) { return Submission::InvalidValue; }
            if (validation.empty) {
                Commit(State::Cancelled, Reason::UserCancel, {}, now);
            } else {
                Commit(State::Accepted, Reason::None, std::string(value), now);
            }
            return Submission::Committed;
        }

        bool Cancel(std::string_view token, Milliseconds now, Reason reason = Reason::UserCancel)
        {
            return End(token, State::Cancelled, reason, now);
        }

        bool Fail(std::string_view token, Milliseconds now, Reason reason = Reason::Unavailable)
        {
            return End(token, State::Failed, reason, now);
        }

        bool Interrupt(std::string_view token, Milliseconds now,
            Reason reason = Reason::OwnerCancelled)
        {
            Tick(now);
            if (Matches(token) && _record->snapshot.state == State::Accepted &&
                !_record->snapshot.deliveryInterrupted) {
                // Tool destruction can race between a valid UI acceptance and
                // the waiting script consuming it. Do not rewrite that first
                // terminal outcome; revoke application to the dead operation.
                auto& snapshot = _record->snapshot;
                snapshot.deliveryInterrupted = true;
                snapshot.deliveryReason = reason;
                ++snapshot.revision;
                return true;
            }
            return End(token, State::Interrupted, reason, now);
        }

        bool CancelOwner(std::uint32_t owner, Milliseconds now)
        {
            if (!_record || _record->snapshot.request.owner != owner) { return false; }
            return Interrupt(_record->snapshot.token, now, Reason::OwnerCancelled);
        }

        // This is an observed release of this exact lease, not merely an engine
        // close request. An old close can release a load-reset quarantine, but
        // cannot touch a newer request. An unexpected close never accepts text.
        bool AcknowledgeClosed(std::string_view token, Milliseconds now)
        {
            if (!_orphanToken.empty() && token == _orphanToken) {
                _orphanToken.clear();
                return true;
            }
            if (!Matches(token)) { return false; }
            auto& snapshot = _record->snapshot;
            bool changed = false;
            if (!snapshot.Terminal()) {
                Commit(State::Interrupted, Reason::UnexpectedClose, {}, now);
                changed = true;
            }
            if (snapshot.released) { return changed; }
            snapshot.released = true;
            snapshot.closeRequested = false;
            ++snapshot.revision;
            if (snapshot.consumed) { _record.reset(); }
            return true;
        }

        // Only a finished outcome can be consumed. A closing timeout permits
        // the caller to unwind, but keeps the lease quarantined until actual
        // closure. No age-based result eviction or accumulated result queue.
        bool Consume(std::string_view token)
        {
            if (!Matches(token) || !_record->snapshot.Finished() || _record->snapshot.consumed) {
                return false;
            }
            auto& snapshot = _record->snapshot;
            snapshot.consumed = true;
            ++snapshot.revision;
            if (snapshot.released) { _record.reset(); }
            return true;
        }

        // A saved script can resume after load and issue Begin immediately
        // before noticing its own operation generation changed. It must abandon
        // that exact newly returned token, never cancel whichever later request
        // happens to have the same owner. Stop delivery and mark the consumer
        // gone while retaining any acquired UI lease until actual release.
        bool Abandon(std::string_view token, Milliseconds now)
        {
            Tick(now);
            if (!Matches(token) || _record->snapshot.consumed) { return false; }
            auto& snapshot = _record->snapshot;
            if (snapshot.state == State::Accepted) {
                snapshot.deliveryInterrupted = true;
                snapshot.deliveryReason = Reason::CallerAbandoned;
            } else if (!snapshot.Terminal()) {
                Commit(State::Interrupted, Reason::CallerAbandoned, {}, now);
            }
            snapshot.consumed = true;
            ++snapshot.revision;
            if (snapshot.released) { _record.reset(); }
            return true;
        }

        bool Tick(Milliseconds now)
        {
            if (!_record) { return false; }
            bool changed = false;
            auto& snapshot = _record->snapshot;
            if (snapshot.state == State::Queued && Elapsed(now, _record->began, _openTimeout)) {
                Commit(State::Failed, Reason::OpenTimeout, {}, now);
                changed = true;
            }
            if (snapshot.Terminal() && !snapshot.released && !snapshot.closeExpired &&
                Elapsed(now, _record->ended, _closeTimeout)) {
                snapshot.closeExpired = true;
                ++snapshot.revision;
                changed = true;
            }
            return changed;
        }

        // Read immediately returns Unknown for every old token. Keep at most
        // one quarantined UI lease so a new prompt cannot open over an old one.
        // Root must close the returned token and observe AcknowledgeClosed.
        std::string Reset()
        {
            if (_record && !_record->snapshot.released) {
                _orphanToken = _record->snapshot.token;
            }
            _record.reset();
            if (_epoch == (std::numeric_limits<std::uint64_t>::max)()) {
                _exhausted = true;
            } else {
                ++_epoch;
            }
            _sequence = 0;
            return _orphanToken;
        }

        bool Busy() const noexcept { return _record.has_value() || !_orphanToken.empty(); }
        std::string QuarantinedToken() const { return _orphanToken; }

    private:
        struct Record
        {
            Snapshot snapshot;
            Milliseconds began;
            Milliseconds ended;
        };

        bool Matches(std::string_view token) const noexcept
        {
            return !token.empty() && _record && _record->snapshot.token == token;
        }

        static bool Elapsed(Milliseconds now, Milliseconds began, Milliseconds duration) noexcept
        {
            // Unsigned subtraction handles the full signed timestamp domain
            // without signed overflow; a backwards test clock does not expire.
            return now >= began &&
                static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(began) >=
                    static_cast<std::uint64_t>(duration);
        }

        bool End(std::string_view token, State state, Reason reason, Milliseconds now)
        {
            Tick(now);
            if (!Matches(token) || _record->snapshot.Terminal()) { return false; }
            Commit(state, reason, {}, now);
            return true;
        }

        void Commit(State state, Reason reason, std::string value, Milliseconds now)
        {
            auto& snapshot = _record->snapshot;
            snapshot.state = state;
            snapshot.reason = reason;
            snapshot.value = std::move(value);
            snapshot.closeRequested = !snapshot.released;
            _record->ended = now;
            ++snapshot.revision;
        }

        std::optional<Record> _record;
        std::string _orphanToken;
        std::uint64_t _epoch{ 1 };
        std::uint64_t _sequence{};
        Milliseconds _openTimeout;
        Milliseconds _closeTimeout;
        bool _exhausted{};
    };
}

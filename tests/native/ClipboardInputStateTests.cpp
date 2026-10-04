// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClipboardInputState.h"
#include "InputEventABI.h"
#include "InputOverlayVisibility.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

namespace
{
    template <class Check>
    void CheckOverlayVisibility(Check&& check)
    {
        struct Overlay { bool root{ true }, child{ true }, readable{ true }, writable{ true }; unsigned writes{}; };
        using Handle = std::shared_ptr<Overlay>;
        const auto read = [](const Handle& p) -> std::optional<bool> {
            return p->readable ? std::optional<bool>{p->root} : std::nullopt;
        };
        const auto write = [](const Handle& p, bool visible) {
            if (!p->writable) return false;
            p->root = visible; ++p->writes; return true;
        };
        Clipboard::Input::OverlayVisibility<Handle> lease;
        lease.Update({}, read, write);
        check(lease.Release(write), "missing optional mod is a no-op");
        auto first = std::make_shared<Overlay>();
        lease.Update(first, read, write);
        check(!first->root && first->child && first->writes == 1, "late widget suppressed at outer root only");
        lease.Update(first, read, write);
        check(first->writes == 1, "unchanged widget does not incur per-frame GFx writes");
        first->child = false; // Its own menu/PA rule changes while input is open.
        check(lease.Release(write) && first->root && !first->child, "release preserves current mod-owned child visibility");
        lease.Release(write);
        check(first->writes == 2, "repeated actual-close/destructor cleanup restores once");
        first->root = false;
        lease.Update(first, read, write);
        lease.Release(write);
        check(!first->root && first->writes == 2, "initially hidden outer root is never forced visible");
        first->root = true;
        lease.Update(first, read, write);
        auto second = std::make_shared<Overlay>();
        lease.Update(second, read, write);
        check(first->root && !second->root, "replacement restores old movie and suppresses new movie");
        std::weak_ptr<Overlay> retained = second;
        second.reset();
        check(!retained.expired(), "suppression retains removed movie until safe release");
        lease.Update({}, read, write);
        check(retained.expired(), "menu removal releases the retained movie");
        first->readable = false;
        lease.Update(first, read, write);
        check(first->root && lease.Release(write), "unrecognized movie leaves visibility untouched");
        first->readable = true; first->writable = false;
        lease.Update(first, read, write);
        check(first->root && lease.Release(write), "failed suppression does not claim restoration ownership");
        first->writable = true;
        lease.Update(first, read, write);
        first->writable = false;
        check(!lease.Release(write), "failed restoration is reported to caller");
        first->writable = true;
        check(lease.Release(write) && !first->root, "failed release cannot later change a newer visibility state");
        // Cancel, forced-close, initialization failure, and normal accept share
        // the production Release path. A new lease must independently snapshot.
        first->root = true;
        lease.Update(first, read, write);
        check(!first->root && lease.Release(write) && first->root, "subsequent dialog has independent visibility ownership");
    }

    // A host-only counterpart of the menu's two polymorphic bases. Construct
    // no RE::IMenu here: its strings, allocation and destruction use the game.
    struct PrimaryMenuBase
    {
        virtual ~PrimaryMenuBase() = default;
        std::uint64_t primarySentinel{ 0x0123456789ABCDEF };
    };
    struct InputEventFixture { std::uint32_t code{ 0x41 }; };
    struct InputUserBase
    {
        virtual ~InputUserBase() = default;
        virtual bool ShouldHandleEvent(const InputEventFixture*) { return false; }
        virtual void HandleEvent(const InputEventFixture*) {}
        bool enabled{ true };
    };
    static_assert(sizeof(PrimaryMenuBase) == 0x10);
    static_assert(sizeof(InputUserBase) == 0x10);

    struct MenuFixture final : PrimaryMenuBase, InputUserBase
    {
        using ShouldFunction = bool (*)(InputUserBase*, const InputEventFixture*);
        using HandleFunction = void (*)(InputUserBase*, const InputEventFixture*);
        InputUserBase* seenReceiver{};
        const InputEventFixture* seenEvent{};
        unsigned shouldEntries{}, handleEntries{}, engineShouldCalls{}, engineHandleCalls{};
        bool engineDecision{};

        static bool EngineShould(InputUserBase* receiver, const InputEventFixture* event)
        {
            auto* menu = static_cast<MenuFixture*>(receiver);
            menu->seenReceiver = receiver;
            menu->seenEvent = event;
            ++menu->engineShouldCalls;
            return menu->engineDecision;
        }
        static void EngineHandle(InputUserBase* receiver, const InputEventFixture* event)
        {
            auto* menu = static_cast<MenuFixture*>(receiver);
            menu->seenReceiver = receiver;
            menu->seenEvent = event;
            ++menu->engineHandleCalls;
        }
        bool ShouldHandleEvent(const InputEventFixture* event) override
        {
            ++shouldEntries;
            const ShouldFunction engine = EngineShould;
            return Clipboard::InputEventABI::Forward<InputUserBase>(this, event, engine);
        }
        void HandleEvent(const InputEventFixture* event) override
        {
            ++handleEntries;
            const HandleFunction engine = EngineHandle;
            Clipboard::InputEventABI::Forward<InputUserBase>(this, event, engine);
        }
    };

    template <class Check>
    void CheckMenuInputABI(Check&& check)
    {
        MenuFixture menu;
        InputEventFixture event;
        auto* receiver = static_cast<InputUserBase*>(&menu);
        check(reinterpret_cast<std::uintptr_t>(receiver) - reinterpret_cast<std::uintptr_t>(&menu) == 0x10,
            "host fixture places input receiver at the reviewed menu +0x10 secondary base");
        check(reinterpret_cast<void*>(receiver) != reinterpret_cast<void*>(&menu),
            "a full-menu pointer must not compare equal to the engine input receiver");

        menu.engineDecision = true;
        check(receiver->ShouldHandleEvent(&event), "ShouldHandleEvent propagates the engine true result");
        check(menu.seenReceiver == receiver && menu.seenEvent == &event,
            "ShouldHandleEvent forwards the secondary base and original event identity");
        check(menu.shouldEntries == 1 && menu.engineShouldCalls == 1,
            "virtual ShouldHandleEvent enters the engine adapter exactly once without recursion");
        menu.engineDecision = false;
        check(!receiver->ShouldHandleEvent(&event), "ShouldHandleEvent propagates the engine false result");
        check(menu.shouldEntries == 2 && menu.engineShouldCalls == 2,
            "a rejected event does not recurse or repeat engine dispatch");

        receiver->HandleEvent(&event);
        check(menu.seenReceiver == receiver && menu.seenEvent == &event,
            "HandleEvent forwards the secondary base and original event identity");
        check(menu.handleEntries == 1 && menu.engineHandleCalls == 1,
            "virtual HandleEvent enters the void engine adapter exactly once without recursion");
        InputEventFixture secondEvent{ 0x42 };
        receiver->HandleEvent(&secondEvent);
        check(menu.seenEvent == &secondEvent && menu.engineHandleCalls == 2,
            "subsequent input dispatch does not retain the prior event pointer");
        check(menu.primarySentinel == 0x0123456789ABCDEF && menu.enabled,
            "secondary input dispatch preserves both primary and input-base data");

        // The adapter preserves pointer identity, including optional null event
        // arguments; admission policy and validity checks remain in the caller.
        const auto* noEvent = static_cast<const InputEventFixture*>(nullptr);
        const MenuFixture::ShouldFunction should = MenuFixture::EngineShould;
        check(!Clipboard::InputEventABI::Forward<InputUserBase>(&menu, noEvent, should) &&
            menu.seenReceiver == receiver && menu.seenEvent == nullptr,
            "the same production adapter preserves a null event argument without fabricating input");
    }
}

int main()
{
    using namespace Clipboard::Input;
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool passed, const char* description) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << description << '\n';
        }
    };
    CheckMenuInputABI(check);
    CheckOverlayVisibility(check);
    const auto numeric = [] {
        Request request;
        request.owner = 0xFF000123;
        request.header = "Rotation";
        request.defaultValue = "90";
        request.inputType = 0;
        request.maxChars = 3;
        request.minimum = 1;
        request.maximum = 180;
        return request;
    };
    const auto ready = [&](Coordinator& coordinator, const Request& request, std::int64_t now = 0) {
        const auto begin = coordinator.Begin(request, now);
        check(static_cast<bool>(begin), "request accepted for dispatch");
        check(coordinator.Read(begin.token).state == State::Queued, "dispatch acknowledgement differs from readiness");
        check(coordinator.MarkOpening(begin.token), "UI task acquires its exact opening lease");
        check(coordinator.MarkReady(begin.token, now), "readiness acknowledged after opening lease");
        const auto revision = coordinator.Read(begin.token).revision;
        check(coordinator.MarkReady(begin.token, now) && coordinator.Read(begin.token).revision == revision,
            "lost readiness acknowledgement can be retried without another transition");
        return begin.token;
    };

    check(static_cast<std::uint32_t>(State::Unknown) == 0 &&
        static_cast<std::uint32_t>(State::Interrupted) == 6, "Papyrus state numbers remain explicit");

    // Dropping every notification still leaves status/result available. Repeated
    // reads never consume the result and cannot accidentally release ownership.
    {
        Coordinator coordinator;
        auto request = numeric();
        const auto token = ready(coordinator, request);
        request.minimum = 100;
        request.maximum = 110;
        request.header = "Changed after dispatch";
        check(coordinator.Read(token).request.header == "Rotation" &&
            coordinator.Read(token).request.maximum == 180, "request rules and prompt are captured by value");
        check(coordinator.Submit(token, "45", 10) == Submission::Committed, "valid result is committed");
        check(coordinator.Read(token).state == State::Accepted && coordinator.Read(token).value == "45",
            "lost accept notification recovered by authoritative query");
        check(!coordinator.Read(token).Finished() && coordinator.Read(token).closeRequested,
            "accepted outcome is separate from menu release");
        check(DeliverableValue(coordinator.Read(token)).empty(), "accepted data cannot apply before menu release");
        check(!coordinator.Consume(token), "cannot consume before release or bounded close expiry");
        check(coordinator.Begin(numeric(), 11).reason == Reason::Busy, "pending menu closure blocks another request");
        check(coordinator.AcknowledgeClosed(token, 20), "actual close acknowledged");
        check(coordinator.Read(token).Finished(), "release makes accepted outcome finished");
        check(DeliveryState(coordinator.Read(token)) == State::Accepted &&
            DeliverableValue(coordinator.Read(token)) == "45", "closed accepted outcome is safely deliverable");
        check(ApprovalAtConsumption(coordinator.Read(token)), "valid released acceptance grants consumption approval");
        for (int i = 0; i < 20; ++i) {
            check(coordinator.Read(token).value == "45", "lost event result survives repeated retrieval");
        }
        check(coordinator.Begin(numeric(), 30).reason == Reason::Busy, "unconsumed result is not silently evicted");
        check(coordinator.Consume(token), "caller acknowledges result consumption");
        check(coordinator.Read(token).state == State::Unknown && !coordinator.Busy(), "consumption bounds retained records");
        check(!ApprovalAtConsumption(coordinator.Read(token)), "consumed record cannot grant another approval");
        check(!coordinator.Consume(token), "duplicate consumption cannot touch another owner");
    }

    // Either winner of an accept/cancel race is immutable. A retransmitted
    // submission receives an ACK to stop UI retry; it never rewrites the winner.
    for (const bool cancelFirst : { false, true }) {
        Coordinator coordinator;
        const auto token = ready(coordinator, numeric());
        if (cancelFirst) {
            check(coordinator.Cancel(token, 1), "first cancel commits");
            check(coordinator.Submit(token, "90", 2) == Submission::AlreadyCommitted,
                "accept after cancel acknowledges existing outcome");
        } else {
            check(coordinator.Submit(token, "90", 1) == Submission::Committed, "first accept commits");
            check(!coordinator.Cancel(token, 2), "cancel after accept cannot replace outcome");
        }
        check(coordinator.Submit(token, "120", 3) == Submission::AlreadyCommitted,
            "duplicate or conflicting submit preserves first terminal result");
        check(coordinator.Read(token).state == (cancelFirst ? State::Cancelled : State::Accepted),
            "race winner remains authoritative");
        check(coordinator.Read(token).value == (cancelFirst ? "" : "90"), "immutable race winner value");
    }

    // Cancel before a queued task begins prevents that task from opening later.
    {
        Coordinator coordinator;
        const auto begin = coordinator.Begin(numeric(), 0);
        check(!coordinator.MarkReady(begin.token, 1), "readiness before ownership is rejected");
        check(coordinator.Submit(begin.token, "90", 1) == Submission::Rejected, "submit before UI readiness is rejected");
        check(coordinator.Cancel(begin.token, 2), "cancel while task remains queued");
        check(coordinator.Read(begin.token).Finished() && !coordinator.Read(begin.token).closeRequested,
            "never-opened cancellation needs no phantom close");
        check(!coordinator.MarkOpening(begin.token), "cancelled delayed task cannot acquire menu");
        check(coordinator.Consume(begin.token), "cancel before opening can be consumed");
        const auto next = ready(coordinator, numeric(), 3);
        check(next != begin.token, "sequential request tokens differ");
        check(!coordinator.MarkReady(begin.token, 4) && !coordinator.AcknowledgeClosed(begin.token, 4),
            "late old ready/close never affect new request");
        check(coordinator.Submit(begin.token, "45", 4) == Submission::Rejected &&
            coordinator.Read(next).state == State::Ready, "late old acceptance never reaches new owner");
    }

    // Bound infrastructure waits without imposing any typing deadline.
    {
        Coordinator coordinator(100, 50);
        const auto begin = coordinator.Begin(numeric(), 0);
        check(!coordinator.Tick(99), "queued request gets complete open allowance");
        check(coordinator.Tick(100), "missing dispatch task expires at opening deadline");
        check(coordinator.Read(begin.token).state == State::Failed &&
            coordinator.Read(begin.token).reason == Reason::OpenTimeout &&
            coordinator.Read(begin.token).Finished(), "lost opening without acquired lease finishes safely");
        check(!coordinator.MarkOpening(begin.token), "opening task cannot run after timeout");
        check(coordinator.Consume(begin.token), "failed opening can be consumed");
        const auto next = ready(coordinator, numeric(), 200);
        check(!coordinator.Tick(86400200), "long user typing has no deadline");
        check(coordinator.Read(next).state == State::Ready, "one day of typing still owns ready prompt");
        check(coordinator.Submit(next, "180", 86400201) == Submission::Committed, "long typing result can be accepted");
    }
    {
        Coordinator coordinator(100, 50);
        const auto begin = coordinator.Begin(numeric(), 0);
        check(coordinator.MarkOpening(begin.token), "opening task has acquired lease");
        check(coordinator.Tick(100), "never-ready movie expires");
        check(coordinator.Read(begin.token).closeRequested && !coordinator.Read(begin.token).Finished(),
            "timed out movie still requires close acknowledgement");
        check(!coordinator.MarkReady(begin.token, 101), "late movie handshake cannot resurrect timed out request");
        check(!coordinator.Tick(149) && coordinator.Tick(150), "closure infrastructure timeout is separately bounded");
        const auto snapshot = coordinator.Read(begin.token);
        check(snapshot.Finished() && snapshot.closeExpired && !snapshot.released && snapshot.closeRequested,
            "caller may unwind while actual lease stays quarantined");
        check(snapshot.state == State::Failed && snapshot.reason == Reason::OpenTimeout,
            "close timeout cannot replace first terminal reason");
        check(coordinator.Consume(begin.token) && coordinator.Busy(), "consuming after timeout retains close quarantine");
        check(coordinator.Begin(numeric(), 151).reason == Reason::Busy, "failed closure blocks next menu opening");
        check(!coordinator.AcknowledgeClosed("wrong:token", 160), "wrong closure cannot free quarantine");
        check(coordinator.AcknowledgeClosed(begin.token, 161) && !coordinator.Busy(),
            "actual late closure frees only matching quarantine");
        check(coordinator.Read(begin.token).state == State::Unknown, "consumed closed record is removed");
    }

    // Accepted data remains accepted even if closing times out: the caller can
    // distinguish release failure via closeExpired and decide how to report it.
    {
        Coordinator coordinator(100, 50);
        const auto token = ready(coordinator, numeric());
        check(coordinator.Submit(token, "90", 1) == Submission::Committed, "accepted closing-timeout fixture");
        check(coordinator.Tick(51), "accepted close timeout fires");
        check(coordinator.Read(token).Finished() && coordinator.Read(token).value == "90" &&
            coordinator.Read(token).state == State::Accepted, "close failure does not erase or forge accepted answer");
        check(DeliveryState(coordinator.Read(token)) == State::Failed &&
            DeliverableValue(coordinator.Read(token)).empty(), "closure failure suppresses accepted answer delivery");
        check(!ApprovalAtConsumption(coordinator.Read(token)), "close timeout denies final consumption approval");
        check(coordinator.AcknowledgeClosed(token, 52) &&
            DeliverableValue(coordinator.Read(token)).empty(), "late close cannot undo delivery failure");
    }

    // Load reset invalidates all old queries/queued work and retains only a
    // token-sized orphan lease until the UI confirms that specific menu closed.
    {
        Coordinator coordinator;
        const auto old = ready(coordinator, numeric());
        check(coordinator.Reset() == old && coordinator.QuarantinedToken() == old,
            "reset returns exact orphan lease for UI cleanup");
        check(coordinator.Read(old).state == State::Unknown, "saved old request becomes unknown after load");
        check(!coordinator.MarkReady(old, 1) && coordinator.Submit(old, "90", 1) == Submission::Rejected,
            "old movie callbacks after reset are rejected");
        check(coordinator.Begin(numeric(), 1).reason == Reason::Busy, "load quarantines prior menu until real close");
        check(coordinator.Reset() == old, "repeated reset retains only the same orphan cleanup lease");
        check(coordinator.AcknowledgeClosed(old, 2), "old actual closure releases reset quarantine");
        const auto next = ready(coordinator, numeric(), 3);
        check(next != old, "session epoch changes tokens across reset");
        check(!coordinator.AcknowledgeClosed(old, 4) && coordinator.Read(next).state == State::Ready,
            "duplicate old reset close cannot affect next session request");
    }
    {
        Coordinator coordinator;
        const auto queued = coordinator.Begin(numeric(), 0);
        check(coordinator.Reset().empty() && !coordinator.Busy(), "reset before opening has no orphan lease");
        check(!coordinator.MarkOpening(queued.token), "pre-load queued task never opens after reset");
        const auto next = coordinator.Begin(numeric(), 1);
        check(next.token != queued.token, "reset changes queued request token identity");
    }

    // A caller may discover a stale Papyrus operation generation just after
    // Begin returned. Exact-token abandonment closes that dispatch without
    // stranding a consumer or touching another prompt from the same tool.
    {
        Coordinator coordinator;
        const auto abandoned = coordinator.Begin(numeric(), 0);
        check(coordinator.Abandon(abandoned.token, 1), "queued stale caller abandons its exact token");
        check(coordinator.Read(abandoned.token).state == State::Unknown && !coordinator.Busy(),
            "abandoned never-opened dispatch needs no future consumer");
        check(!coordinator.MarkOpening(abandoned.token), "abandoned delayed opening task cannot run");
        const auto newer = ready(coordinator, numeric(), 2);
        check(!coordinator.Abandon(abandoned.token, 3) && coordinator.Read(newer).state == State::Ready,
            "late old abandonment cannot cancel a newer same-owner prompt");
    }
    for (const bool alreadyAccepted : { false, true }) {
        Coordinator coordinator(100, 50);
        const auto token = ready(coordinator, numeric());
        if (alreadyAccepted) {
            check(coordinator.Submit(token, "90", 1) == Submission::Committed, "accepted abandonment fixture");
        }
        check(coordinator.Abandon(token, 2), "active caller abandonment acknowledged");
        check(coordinator.Read(token).consumed && coordinator.Read(token).closeRequested,
            "abandoned active request retains only release ownership");
        check(DeliveryState(coordinator.Read(token)) == State::Interrupted &&
            DeliverableValue(coordinator.Read(token)).empty(), "abandoned acceptance can never be delivered");
        check(!coordinator.Abandon(token, 3), "duplicate abandonment is idempotent");
        check(coordinator.Tick(52) && coordinator.Busy(), "abandoned close timeout preserves actual ownership quarantine");
        check(coordinator.Begin(numeric(), 53).reason == Reason::Busy, "no next prompt before abandoned UI physically closes");
        check(coordinator.AcknowledgeClosed(token, 54) && !coordinator.Busy() &&
            coordinator.Read(token).state == State::Unknown, "actual closure retires abandoned record without caller polling");
    }

    // Exact owner is carried in the request; other tools cannot cancel it.
    {
        Coordinator coordinator;
        const auto token = ready(coordinator, numeric());
        check(!coordinator.CancelOwner(0xFF000124, 1), "unrelated owner cancellation ignored");
        check(coordinator.CancelOwner(0xFF000123, 2), "destroyed owning tool interrupts its request");
        check(coordinator.Read(token).state == State::Interrupted &&
            coordinator.Read(token).reason == Reason::OwnerCancelled, "owner cancellation has distinct terminal outcome");
        check(coordinator.Submit(token, "90", 3) == Submission::AlreadyCommitted &&
            coordinator.Read(token).value.empty(), "late accepted UI text after owner destruction never applies");
    }
    for (const bool alreadyClosed : { false, true }) {
        Coordinator coordinator;
        const auto token = ready(coordinator, numeric());
        check(coordinator.Submit(token, "90", 1) == Submission::Committed, "owner destruction after acceptance fixture");
        if (alreadyClosed) { check(coordinator.AcknowledgeClosed(token, 2), "owner destruction after close fixture"); }
        check(coordinator.CancelOwner(0xFF000123, 3), "owner destruction revokes still-unconsumed accepted result");
        check(coordinator.Read(token).state == State::Accepted && coordinator.Read(token).value == "90",
            "delivery revocation preserves immutable UI outcome");
        check(DeliveryState(coordinator.Read(token)) == State::Interrupted &&
            coordinator.Read(token).deliveryReason == Reason::OwnerCancelled &&
            DeliverableValue(coordinator.Read(token)).empty(), "revoked operation cannot consume old accepted answer");
        check(!coordinator.CancelOwner(0xFF000123, 4), "duplicate owner invalidation is idempotent");
    }
    {
        Coordinator coordinator;
        const auto token = ready(coordinator, numeric());
        check(coordinator.Submit(token, "90", 1) == Submission::Committed, "copy-then-cancel acceptance fixture");
        check(coordinator.AcknowledgeClosed(token, 2), "copy-then-cancel release fixture");
        const auto previouslyCopied = DeliverableValue(coordinator.Read(token));
        check(previouslyCopied == "90" && ApprovalAtConsumption(coordinator.Read(token)),
            "result was genuinely deliverable at the earlier retrieval call");
        check(coordinator.CancelOwner(0xFF000123, 3), "owner invalidates between result retrieval and final acknowledgement");
        const bool approved = ApprovalAtConsumption(coordinator.Read(token));
        const bool consumed = coordinator.Consume(token);
        check(consumed && !approved && previouslyCopied == "90",
            "final acknowledgement retires but denies stale copied acceptance");
    }
    {
        Coordinator coordinator;
        const auto token = ready(coordinator, numeric());
        check(coordinator.AcknowledgeClosed(token, 1), "unexpected menu close observed");
        check(coordinator.Read(token).Finished() && coordinator.Read(token).state == State::Interrupted &&
            coordinator.Read(token).reason == Reason::UnexpectedClose && coordinator.Read(token).value.empty(),
            "closing without acceptance produces interruption not default text");
    }
    {
        Coordinator coordinator;
        const auto begin = coordinator.Begin(numeric(), 0);
        check(coordinator.Fail(begin.token, 1, Reason::DispatchRejected), "explicit dispatch rejection retained");
        check(coordinator.Read(begin.token).Finished() &&
            coordinator.Read(begin.token).reason == Reason::DispatchRejected, "failed dispatch does not await an impossible callback");
    }

    // Decimal prompts validate the original text before any float rounding.
    {
        auto request = numeric();
        request.inputType = 1;
        request.minimum = 0;
        request.maxChars = 64;
        for (const std::string value : { "0.000001", ".5", "1.234567", "090.123456", "180", "180.000000" }) {
            check(Validate(request, value).valid, "decimal input accepts up to six fractional places");
        }
        for (const std::string value : { "0", "0.000000", "180.000001", "179.9999999", "1.0000000",
            "-1", "+1", "1e2", "NaN", "inf", "1,5", " 1.5", "1.5 ", ".", "1.", "1..5",
            "4294967296.1", "99999999999999999999.1", "1.2x" }) {
            check(!Validate(request, value).valid, "invalid decimal rejected without stripping, rounding or truncation");
        }
        request.maximum = 1000;
        check(Validate(request, "1000.000000").valid && !Validate(request, "1000.000001").valid,
            "scale-up upper bound compared exactly");
        request.maximum = 99;
        check(Validate(request, "98.999999").valid && !Validate(request, "99.000001").valid,
            "scale-down upper bound compared exactly");
        request.minimum = 10;
        check(!Validate(request, "9.999999").valid && Validate(request, "10.000000").valid,
            "nonzero decimal lower bound respected");
        Coordinator coordinator;
        request.minimum = 0;
        const auto token = ready(coordinator, request);
        check(coordinator.Submit(token, "12.3456789", 1) == Submission::InvalidValue &&
            coordinator.Read(token).state == State::Ready, "seventh place keeps prompt open");
        check(coordinator.Submit(token, "12.345678", 2) == Submission::Committed &&
            coordinator.Read(token).value == "12.345678", "accepted decimal text retained exactly");
        Coordinator blank;
        const auto blankToken = ready(blank, request);
        check(blank.Submit(blankToken, "", 1) == Submission::Committed &&
            blank.Read(blankToken).state == State::Cancelled, "blank decimal cancels");
    }

    // Independent native validation covers paste or a compromised/stale movie.
    {
        auto request = numeric();
        check(Validate(request, "1").valid && Validate(request, "180").valid, "numeric inclusive bounds");
        for (const std::string value : { "0", "181", "-1", "+1", "1.0", "1e2", " 90", "90 ", "90x", "9999" }) {
            check(!Validate(request, value).valid, "numeric full-string/range/length validation");
        }
        check(!Validate(request, "\xD9\xA1").valid, "non-ASCII Arabic digit is rejected for numeric protocol");
        check(Validate(request, "090").valid && Validate(request, "090").number == 90, "leading zero decimal preserved safely");
        request.maxChars = 20;
        request.minimum = 0;
        request.maximum = (std::numeric_limits<std::uint32_t>::max)();
        check(Validate(request, "0").valid && Validate(request, "4294967295").valid, "generic unsigned full range supported");
        check(!Validate(request, "4294967296").valid && !Validate(request, "99999999999999999999").valid,
            "overflow rejected before multiplication");
        check(Validate(request, "").valid && Validate(request, "").empty, "empty entry distinguished from numeric zero");
    }
    {
        Request request;
        request.maxChars = 2;
        check(Validate(request, "\xC3\xA9\xE6\xBC\xA2").valid, "UTF-8 accented and CJK text count as two UTF-16 units");
        check(Validate(request, "\xF0\x9F\x98\x80").valid &&
            Validate(request, "\xF0\x9F\x98\x80").utf16Units == 2, "supplementary code point counts as two UTF-16 units");
        check(!Validate(request, "a\xF0\x9F\x98\x80").valid, "UTF-16 limit applies consistently to supplementary text");
        request.maxChars = 50;
        check(Validate(request, "Vault [A] = B; 100% <name> & 'quote'").valid,
            "legitimate pattern-name punctuation is retained as data");
        for (const std::string value : { "a\nb", "a\rb", "a\tb", "\x7F", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9" }) {
            check(!Validate(request, value).valid, "single-line control and separator rejection");
        }
        check(!Validate(request, std::string("a\0b", 3)).valid, "embedded NUL cannot bypass native name validation");
        for (const std::string value : { "\xC0\xAF", "\x80", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF0\x80\x80\xAF", "\xF0\x9F" }) {
            check(!Validate(request, value).valid, "malformed, overlong, surrogate and out-of-range UTF-8 rejected");
        }
        request.maxChars = 0;
        check(!Validate(request, "").valid, "invalid request rules do not accept even empty submission");
    }
    {
        Coordinator coordinator;
        auto request = numeric();
        request.maxChars = 0;
        check(coordinator.Begin(request, 0).reason == Reason::InvalidArguments && !coordinator.Busy(),
            "invalid request rejected without reserving ownership");
        request = numeric();
        request.inputType = 3;
        check(coordinator.Begin(request, 0).reason == Reason::InvalidArguments, "unsupported input type rejected");
        request = numeric();
        request.minimum = 181;
        check(coordinator.Begin(request, 0).reason == Reason::InvalidArguments, "inverted numeric bounds rejected");
        request = numeric();
        request.defaultValue = "A generated default longer than allowed";
        const auto token = ready(coordinator, request);
        check(coordinator.Submit(token, request.defaultValue, 1) == Submission::InvalidValue,
            "initial default may be edited but invalid default cannot be accepted");
        check(coordinator.Read(token).state == State::Ready, "invalid submission keeps prompt ready for correction");
        check(coordinator.Submit(token, "", 2) == Submission::Committed &&
            coordinator.Read(token).state == State::Cancelled, "blank numeric submit cancels safely");
    }
    {
        Coordinator coordinator;
        Request request;
        const auto token = ready(coordinator, request);
        check(coordinator.Submit(token, "", 1) == Submission::Committed &&
            coordinator.Read(token).state == State::Cancelled, "blank name cannot authorize pattern overwrite");
    }

    // Monotonic-time arithmetic never expires backwards and tolerates the
    // representable signed endpoints without signed integer overflow.
    {
        Coordinator coordinator(100, 50);
        const auto begin = coordinator.Begin(numeric(), 1000);
        check(!coordinator.Tick(0) && coordinator.Read(begin.token).state == State::Queued,
            "backwards injected clock cannot expire queued request");
        check(coordinator.Tick((std::numeric_limits<std::int64_t>::max)()), "very late clock expires request without overflow");
    }

    std::cout << checks << " Clipboard input state checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

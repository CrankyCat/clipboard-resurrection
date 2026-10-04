// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"
#include "ClipboardInputService.h"
#include "ClipboardInputState.h"
#include "ClipboardInputUI.h"
#include "LocalizationRuntime.h"
#include "LoggingPolicy.h"
#include "PapyrusDispatchABI.h"

namespace Clipboard::InputService
{
    namespace
    {
        std::mutex mutex;
        Input::Coordinator coordinator;
        std::string activeToken;
        RE::ObjectRefHandle ownerHandle;
        std::string notifiedToken;
        std::uint64_t notifiedRevision{};
        std::string closingToken;
        std::int64_t lastCloseAttempt{};

        std::int64_t Now()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::int32_t DeliveredState(const Input::Snapshot& state)
        {
            // A recorded answer is not permission to act while the UI could not
            // release focus. Retain it for diagnostics without delivering it.
            return static_cast<std::int32_t>(Input::DeliveryState(state));
        }

        struct Notice { std::string token; std::int32_t state{}; };
        void F4SEAPI NotifyRegistrant(std::uint64_t handle, const char* script,
            const char* callback, void* data)
        {
            try {
                const auto* notice = static_cast<const Notice*>(data);
                if (!notice || !script || !callback) { return; }
                const auto* gameVM = RE::GameVM::GetSingleton();
                const auto vm = gameVM ? gameVM->GetVM() : nullptr;
                RE::BSTSmartPointer<RE::BSScript::Object> receiver;
                if (!vm || !vm->FindBoundObject(handle, script, false, receiver, true) || !receiver) { return; }
                RE::BSScript::Variable token, state;
                token = RE::BSFixedString(notice->token);
                state = notice->state;
                using Arguments = RE::BSScrapArray<RE::BSScript::Variable>;
                using Signature = bool(Arguments&);
                PapyrusDispatchABI::BorrowedFunction<Signature> builder{
                    std::function<Signature>{ [token, state](Arguments& values) {
                        try {
                            values.clear();
                            values.emplace_back(token);
                            values.emplace_back(state);
                            return true;
                        } catch (...) { values.clear(); return false; }
                    } }
                };
                const auto& arguments = *static_cast<const RE::BSTThreadScrapFunction<Signature>*>(
                    builder.View(REL::Module::get().version() < REL::Version{ 1, 10, 980, 0 }));
                // The reviewed bound-object call consumes the argument builder
                // synchronously. Notification loss/reordering is harmless: the
                // token's retained record, not this method call, owns the result.
                (void)vm->DispatchMethodCall(receiver, RE::BSFixedString(callback), arguments, nullptr);
            } catch (...) { /* A notification cannot invalidate its retained result. */ }
        }

        void Notify(const Input::Snapshot& state)
        {
            try {
                if (state.token.empty()) { return; }
                Notice notice{ state.token, DeliveredState(state) };
                const auto* papyrus = F4SE::GetPapyrusInterface();
                if (papyrus && papyrus->Version() >= F4SE::PapyrusInterface::kVersion) {
                    papyrus->GetExternalEventRegistrations("Clipboard::InputState", &notice, NotifyRegistrant);
                }
                CLIPBOARD_DEBUG_LOG(F4SE::log::info(
                    "Clipboard input: token {} owner {:08X} state {} reason {} released {} close-expired {} revision {}",
                    state.token, state.request.owner, notice.state, static_cast<unsigned>(state.reason),
                    state.released, state.closeExpired, state.revision));
            } catch (...) { }
        }

        Input::Snapshot Pump(std::string_view token)
        {
            // Resolve a generation-aware handle outside the state lock. No raw
            // form/movie pointer is held by the coordinator or queued tasks.
            RE::ObjectRefHandle handle;
            bool checkOwner = false;
            {
                const std::lock_guard lock(mutex);
                if (token == activeToken && !token.empty()) { handle = ownerHandle; checkOwner = true; }
            }
            bool invalidOwner = false;
            if (checkOwner) {
                const auto owner = handle.get();
                invalidOwner = !owner || owner->IsDeleted();
            }
            Input::Snapshot snapshot;
            std::string close;
            bool notify = false;
            const auto now = Now();
            {
                const std::lock_guard lock(mutex);
                if (invalidOwner && token == activeToken) { coordinator.Interrupt(token, now); }
                coordinator.Tick(now);
                snapshot = coordinator.Read(token);
                auto desiredClose = snapshot.closeRequested ? snapshot.token : coordinator.QuarantinedToken();
                if (!desiredClose.empty() && (closingToken != desiredClose || now - lastCloseAttempt >= 500)) {
                    closingToken = desiredClose;
                    lastCloseAttempt = now;
                    close = std::move(desiredClose);
                }
                if (!snapshot.token.empty() &&
                    (notifiedToken != snapshot.token || notifiedRevision != snapshot.revision)) {
                    notifiedToken = snapshot.token;
                    notifiedRevision = snapshot.revision;
                    notify = true;
                }
            }
            if (notify) { Notify(snapshot); }
            if (!close.empty()) { (void)InputUI::Close(close); }
            return snapshot;
        }
    }

    bool Available() { return InputUI::Available(); }

    std::string Begin(RE::TESObjectREFR* owner, std::string_view header, std::string_view initial,
        std::int32_t inputType, std::int32_t maxChars, std::int32_t minimum, std::int32_t maximum)
    {
        if (!owner || owner->IsDeleted() || !Available() || maxChars <= 0 || maxChars > 4096 ||
            minimum < 0 || maximum < 0 || header.size() > 16384 || initial.size() > 16384) { return {}; }
        const auto handle = owner->GetHandle();
        if (!handle) { return {}; }
        Input::Request request;
        request.owner = owner->formID;
        request.header = header;
        request.defaultValue = initial;
        request.inputType = inputType;
        request.maxChars = static_cast<std::uint32_t>(maxChars);
        request.minimum = static_cast<std::uint32_t>(minimum);
        request.maximum = static_cast<std::uint32_t>(maximum);
        request.acceptLabel = Localization::GetRuntimeText("$Clipboard_InputAccept", {});
        request.cancelLabel = Localization::GetRuntimeText("$Clipboard_InputCancel", {});
        request.invalidLabel = Localization::GetRuntimeText("$Clipboard_InputInvalid", {});
        View view{ {}, request.header, request.defaultValue, request.acceptLabel,
            request.cancelLabel, request.invalidLabel,
            Localization::GetRuntimeText("$Clipboard_InputControllerHelp", {}),
            inputType, maxChars, minimum, maximum };
        // Retry abandoned close cleanup before considering another prompt.
        std::string previous;
        { const std::lock_guard lock(mutex); previous = activeToken; }
        (void)Pump(previous);
        Input::BeginResult result;
        {
            const std::lock_guard lock(mutex);
            result = coordinator.Begin(std::move(request), Now());
            if (result) {
                activeToken = result.token;
                ownerHandle = handle;
            }
        }
        if (!result) { return {}; }
        view.token = result.token;
        // Return this request's token even on rejected UI dispatch: its explicit
        // failed state is retrievable and must be consumed by the same owner.
        if (!InputUI::Open(view)) {
            const std::lock_guard lock(mutex);
            coordinator.Fail(result.token, Now(), Input::Reason::DispatchRejected);
        }
        (void)Pump(result.token);
        return result.token;
    }

    std::int32_t State(std::string_view token) { return DeliveredState(Pump(token)); }
    std::string Result(std::string_view token)
    {
        const auto snapshot = Pump(token);
        return Input::DeliverableValue(snapshot);
    }
    bool Finished(std::string_view token)
    {
        const auto snapshot = Pump(token);
        return snapshot.state == Input::State::Unknown || snapshot.Finished();
    }
    bool Acknowledge(std::string_view token)
    {
        (void)Pump(token);
        const std::lock_guard lock(mutex);
        const auto snapshot = coordinator.Read(token);
        const bool deliverable = Input::ApprovalAtConsumption(snapshot);
        const bool consumed = coordinator.Consume(token);
        if (consumed && coordinator.Read(token).state == Input::State::Unknown && activeToken == token) {
            activeToken.clear();
            ownerHandle = {};
        }
        // The result may have been copied just before owner invalidation. ACK
        // consumption and approval to apply that copy atomically under the lock.
        return consumed && deliverable;
    }
    void Cancel(RE::TESObjectREFR* owner)
    {
        if (!owner) { return; }
        const auto handle = owner->GetHandle();
        std::string token;
        {
            const std::lock_guard lock(mutex);
            if (handle == ownerHandle) { coordinator.CancelOwner(owner->formID, Now()); }
            token = activeToken;
        }
        (void)Pump(token);
    }
    void Reset()
    {
        std::string close;
        {
            const std::lock_guard lock(mutex);
            close = coordinator.Reset();
            activeToken.clear();
            ownerHandle = {};
            notifiedToken.clear();
            notifiedRevision = 0;
            closingToken.clear();
        }
        if (!close.empty()) { (void)InputUI::Close(close); }
    }
    void Abandon(std::string_view token)
    {
        {
            const std::lock_guard lock(mutex);
            coordinator.Abandon(token, Now());
        }
        (void)Pump(token);
    }
    bool Opening(std::string_view token)
    {
        (void)Pump(token);
        const std::lock_guard lock(mutex);
        return coordinator.MarkOpening(token);
    }
    bool Ready(std::string_view token, std::int32_t protocol)
    {
        bool accepted;
        {
            const std::lock_guard lock(mutex);
            if (protocol != kProtocol) {
                coordinator.Fail(token, Now(), Input::Reason::ProtocolMismatch);
                accepted = false;
            } else { accepted = coordinator.MarkReady(token, Now()); }
        }
        (void)Pump(token);
        return accepted;
    }
    bool Submit(std::string_view token, std::string_view value, bool cancelled)
    {
        (void)Pump(token);
        bool acknowledged;
        {
            const std::lock_guard lock(mutex);
            if (cancelled) {
                acknowledged = coordinator.Cancel(token, Now()) || coordinator.Read(token).Terminal();
            } else {
                const auto result = coordinator.Submit(token, value, Now());
                acknowledged = result == Input::Submission::Committed || result == Input::Submission::AlreadyCommitted;
            }
        }
        (void)Pump(token);
        return acknowledged;
    }
    void Closed(std::string_view token)
    {
        {
            const std::lock_guard lock(mutex);
            coordinator.AcknowledgeClosed(token, Now());
        }
        (void)Pump(token);
    }
    void Failed(std::string_view token, std::string_view reason)
    {
        // The adapter supplies a bounded machine reason, never the entered text.
        F4SE::log::warn("Clipboard input UI failure: token {} reason {}", token, reason);
        {
            const std::lock_guard lock(mutex);
            coordinator.Fail(token, Now(), Input::Reason::Unavailable);
        }
        (void)Pump(token);
    }
}

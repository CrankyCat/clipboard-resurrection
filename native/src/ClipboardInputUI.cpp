// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Everett C Sands
#include "PCH.h"
#include "ClipboardInputUI.h"
#include "InputEventABI.h"
#include "InputOverlayVisibility.h"
#include <RE/Bethesda/BSScaleformManager.h>
#include <RE/Bethesda/ControlMap.h>
#include <RE/Bethesda/SendHUDMessage.h>
#include <RE/Bethesda/UI.h>
#include <RE/Bethesda/UIMessageQueue.h>

// Uses Clipboard's existing user-INI/default-INI precedence; read once per prompt.
bool HideConditionBoyDuringInput();

namespace Clipboard::InputUI
{
    namespace
    {
        constexpr auto kMenu = "ClipboardInputMenu";
        constexpr auto kMovie = "ClipboardInput";
        using Value = RE::Scaleform::GFx::Value;
        using Movie = RE::Scaleform::Ptr<RE::Scaleform::GFx::Movie>;
        std::atomic_bool registered{};
        // These values are owned exclusively by the UI lane, including the
        // queued-show interval. Retain the lease until removal, not submission.
        std::optional<InputService::View> pending;
        std::string lease;
        bool closing{};
        bool hideQueued{};

        bool ConditionBoyRoot(const Movie& movie, Value& root)
        {
            Value manager;
            return movie && movie->GetVariable(&root, "root1") && root.IsDisplayObject() &&
                root.GetMember("CharacterManager", &manager) && manager.IsDisplayObject();
        }

        std::optional<bool> ReadOverlayVisibility(const Movie& movie)
        {
            Value root, visible;
            if (!ConditionBoyRoot(movie, root) || !root.GetMember("visible", &visible) ||
                !visible.IsBoolean()) return std::nullopt;
            return visible.GetBoolean();
        }

        bool WriteOverlayVisibility(const Movie& movie, bool visible)
        {
            Value root;
            return ConditionBoyRoot(movie, root) && root.SetMember("visible", Value(visible));
        }

        bool IsProtocol(const Value& value)
        {
            return (value.IsInt() && value.GetInt() == InputService::kProtocol) ||
                (value.IsUInt() && value.GetUInt() == InputService::kProtocol) ||
                (value.IsNumber() && value.GetNumber() == InputService::kProtocol);
        }

        bool QueueMessage(RE::UI_MESSAGE_TYPE type, std::string_view token)
        {
            auto* queue = RE::UIMessageQueue::GetSingleton();
            if (!queue) return false;
            // Carry the token through the engine's queue too. A name-only Hide
            // left behind by load/forced close must not close a later request.
            RE::BSUIMessageData::SendUIStringMessage(RE::BSFixedString(kMenu), type, RE::BSFixedString(token));
            return true;
        }

        const RE::BSUIMessageData* MessageData(const RE::UIMessage& message)
        {
            return message.QData() ? RE::fallout_cast<const RE::BSUIMessageData*>(message.QData()) : nullptr;
        }

        void RequestHide(std::string_view token)
        {
            if (lease != token || hideQueued) return;
            hideQueued = QueueMessage(RE::UI_MESSAGE_TYPE::kHide, token);
        }

        void ReleaseLease(std::string_view token)
        {
            if (lease != token) return;
            pending.reset();
            lease.clear();
            closing = false;
            hideQueued = false;
            InputService::Closed(token);
        }

        // This handler owns only the immutable request token. A menu pointer in
        // a GFx function would form a menu/movie refcount cycle or stale pointer.
        class Callback final : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            explicit Callback(std::string token) : token_(std::move(token)) {}
            void Call(const Params& args) override
            {
                if (args.retVal) *args.retVal = Value(false);
                try {
                    if (args.argCount < 2 || !args.args[0].IsString() ||
                        token_ != args.args[0].GetString() || lease != token_) return;
                    bool result{};
                    if (reinterpret_cast<std::uintptr_t>(args.userData) == 1) {
                        if (closing || !IsProtocol(args.args[1])) return;
                        result = InputService::Ready(token_, InputService::kProtocol);
                    } else {
                        if (args.argCount != 3 || !args.args[1].IsString() || !args.args[2].IsBoolean()) return;
                        result = InputService::Submit(token_, args.args[1].GetString(), args.args[2].GetBoolean());
                    }
                    if (args.retVal) *args.retVal = Value(result);
                } catch (...) {
                    InputService::Failed(token_, "ui-callback-exception");
                    closing = true;
                    RequestHide(token_);
                }
            }
        private:
            std::string token_;
        };

        class InputMenu final : public RE::IMenu
        {
        public:
            explicit InputMenu(InputService::View view) : view_(std::move(view))
            {
                static_assert(sizeof(RE::IMenu) == 0x70);
                static_assert(offsetof(RE::IMenu, uiMovie) == 0x40);
                static_assert(offsetof(RE::IMenu, menuFlags) == 0x58);
                menuName = kMenu;
                menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame, RE::UI_MENU_FLAGS::kUsesCursor,
                    RE::UI_MENU_FLAGS::kUsesMenuContext, RE::UI_MENU_FLAGS::kModal,
                    RE::UI_MENU_FLAGS::kDisablePauseMenu, RE::UI_MENU_FLAGS::kRequiresUpdate,
                    RE::UI_MENU_FLAGS::kUsesBlurredBackground);
                depthPriority = RE::UI_DEPTH_PRIORITY::kMessage;
                inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kBasicMenuNav;
                auto* manager = RE::BSScaleformManager::GetSingleton();
                loaded_ = manager && manager->LoadMovie(*this, uiMovie, kMovie, nullptr);
                if (!loaded_ || !uiMovie) return;
                for (const auto path : { "root", "root1", "_root" }) {
                    if (uiMovie->GetVariable(&menuObj, path) && menuObj.IsObject()) break;
                }
                Value protocol;
                loaded_ = menuObj.IsObject() && menuObj.GetMember("clipboardProtocol", &protocol) &&
                    IsProtocol(protocol);
            }

            ~InputMenu() override { try { Release(); } catch (...) {} }

            void OnAddedToMenuStack() override
            {
                RE::IMenu::OnAddedToMenuStack();
                try {
                    if (lease != view_.token || closing || !loaded_) { Fail("ui-asset-missing-or-incompatible"); return; }
                    // TIM inherited SPECIALMenu's SpecialMode push/pop. Blur and
                    // pause alone leave gameplay HUD widgets visible. Own one
                    // mode entry until actual removal (also covered by Release).
                    if (!hudModePushed_) {
                        RE::SendHUDMessage::PushHUDMode(hudMode_);
                        hudModePushed_ = true;
                    }
                    hideConditionBoy_ = HideConditionBoyDuringInput();
                    UpdateConditionBoy();
                    if (auto* controls = RE::ControlMap::GetSingleton()) {
                        controls->SetTextEntryMode(true); textEntry_ = true;
                    } else { Fail("ui-control-map-unavailable"); return; }
                    Value ready, submit, data, result;
                    auto* handler = new Callback(view_.token);
                    uiMovie->CreateFunction(&ready, handler, reinterpret_cast<void*>(std::uintptr_t(1)));
                    uiMovie->CreateFunction(&submit, handler, reinterpret_cast<void*>(std::uintptr_t(2)));
                    handler->Release();
                    uiMovie->CreateObject(&data);
                    const bool attached = menuObj.SetMember("ClipboardReady", ready) &&
                        menuObj.SetMember("ClipboardSubmit", submit) &&
                        data.SetMember("protocol", Value(InputService::kProtocol)) &&
                        data.SetMember("token", Value(view_.token.c_str())) &&
                        data.SetMember("header", Value(view_.header.c_str())) &&
                        data.SetMember("initial", Value(view_.initial.c_str())) &&
                        data.SetMember("acceptLabel", Value(view_.acceptLabel.c_str())) &&
                        data.SetMember("cancelLabel", Value(view_.cancelLabel.c_str())) &&
                        data.SetMember("errorLabel", Value(view_.errorLabel.c_str())) &&
                        data.SetMember("controllerHelp", Value(view_.controllerHelp.c_str())) &&
                        data.SetMember("inputType", Value(view_.inputType)) &&
                        data.SetMember("maxChars", Value(view_.maxChars));
                    if (!attached || !menuObj.Invoke("Initialize", &result, &data, 1) ||
                        !result.IsBoolean() || !result.GetBoolean()) Fail("ui-initialize-rejected");
                } catch (...) { Fail("ui-open-exception"); }
            }

            void OnRemovedFromMenuStack() override
            {
                RE::IMenu::OnRemovedFromMenuStack();
                Release();
            }

            void AdvanceMovie(float delta, std::uint64_t time) override
            {
                RE::IMenu::AdvanceMovie(delta, time);
                // Also catches a widget created/replaced after input opened.
                // No setting reads or repeated GFx writes for a retained movie.
                if (!released_ && hideConditionBoy_) UpdateConditionBoy();
            }

            RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override
            {
                if (message.type == RE::UI_MESSAGE_TYPE::kHide || message.type == RE::UI_MESSAGE_TYPE::kForceHide) {
                    const auto* data = MessageData(message);
                    if (data && view_.token != data->fixedString.c_str()) return RE::UI_MESSAGE_RESULTS::kIgnore;
                    // Removal is the acknowledgment. Do not release on receipt
                    // of Hide alone: engine ownership/focus may still be live.
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
                return RE::IMenu::ProcessMessage(message);
            }

            bool ShouldHandleEvent(const RE::InputEvent* event) override
            {
                if (!event) return false;
                // These two relocated input methods receive the secondary
                // BSInputEventUser subobject, not the complete IMenu. The
                // pinned CommonLib wrappers pass the latter. Keep the ABI
                // correction local to Clipboard instead of changing the vendor.
                using Function = bool (*)(RE::BSInputEventUser*, const RE::InputEvent*);
                const REL::Relocation<Function> function{ REL::ID(1241790, 2287392) };
                return InputEventABI::Forward<RE::BSInputEventUser>(this, event, function.get());
            }

            void HandleEvent(const RE::ButtonEvent* event) override
            {
                if (!event) return;
                if (event->device != RE::INPUT_DEVICE::kGamepad || !menuObj.IsObject()) {
                    ForwardButtonEvent(event);
                    return;
                }
                const char* action{};
                switch (event->GetBSButtonCode()) {
                case RE::BS_BUTTON_CODE::kAButton: action = "Select"; break;
                case RE::BS_BUTTON_CODE::kBButton: action = "Cancel"; break;
                case RE::BS_BUTTON_CODE::kXButton: action = "Delete"; break;
                case RE::BS_BUTTON_CODE::kYButton: action = "Case"; break;
                case RE::BS_BUTTON_CODE::kDPAD_Left: action = "Left"; break;
                case RE::BS_BUTTON_CODE::kDPAD_Right: action = "Right"; break;
                case RE::BS_BUTTON_CODE::kDPAD_Up: action = "Up"; break;
                case RE::BS_BUTTON_CODE::kDPAD_Down: action = "Down"; break;
                default: break;
                }
                // XInput Start is 0x10; CommonLib's enum omits its name.
                if (event->QIDCode() == 0x10) action = "Accept";
                if (action) {
                    if (event->QJustPressed()) {
                        const Value value(action);
                        menuObj.Invoke("ControllerAction", nullptr, &value, 1);
                    }
                } else ForwardButtonEvent(event);
            }

        private:
            void UpdateConditionBoy()
            {
                if (!hideConditionBoy_) return;
                try {
                    const auto* ui = RE::UI::GetSingleton();
                    const auto menu = ui ? ui->GetMenu(RE::BSFixedString("BingleBodyPartsUI")) : nullptr;
                    const Movie movie = menu ? menu->uiMovie : Movie{};
                    conditionBoy_.Update(movie, ReadOverlayVisibility, WriteOverlayVisibility);
                } catch (...) {
                    // Compatibility must not invalidate an otherwise usable input.
                    hideConditionBoy_ = false;
                    logger::warn("Clipboard input: Condition Boy visibility suppression failed");
                }
            }

            void ForwardButtonEvent(const RE::ButtonEvent* event)
            {
                if (!menuObj.IsObject()) return;
                using Function = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);
                const REL::Relocation<Function> function{ REL::ID(1414130, 2287393) };
                InputEventABI::Forward<RE::BSInputEventUser>(this, event, function.get());
            }

            void Fail(std::string_view reason)
            {
                InputService::Failed(view_.token, reason);
                closing = true;
                RequestHide(view_.token);
            }
            void Release()
            {
                if (released_) return;
                released_ = true;
                try {
                    if (!conditionBoy_.Release(WriteOverlayVisibility))
                        logger::warn("Clipboard input: Condition Boy visibility restoration failed");
                } catch (...) {
                    logger::warn("Clipboard input: Condition Boy visibility restoration threw an exception");
                }
                if (hudModePushed_) {
                    RE::SendHUDMessage::PopHUDMode(hudMode_);
                    hudModePushed_ = false;
                }
                if (textEntry_) {
                    if (auto* controls = RE::ControlMap::GetSingleton()) controls->SetTextEntryMode(false);
                    textEntry_ = false;
                }
                ReleaseLease(view_.token);
            }
            InputService::View view_;
            Input::OverlayVisibility<Movie> conditionBoy_;
            bool hideConditionBoy_{};
            RE::HUDModeType hudMode_{ "SpecialMode" };
            bool hudModePushed_{};
            bool loaded_{}, textEntry_{}, released_{};
        };

        RE::IMenu* Create(const RE::UIMessage& message)
        {
            const auto* data = MessageData(message);
            if (!pending || !data || lease != data->fixedString.c_str()) return nullptr;
            // Even a cancelled queued show gets a token-bearing instance, whose
            // removal acknowledges that its engine/menu ownership is finished.
            try { return new InputMenu(*pending); }
            catch (...) {
                const auto token = lease;
                InputService::Failed(token, "ui-menu-create-failed");
                ReleaseLease(token);
                return nullptr;
            }
        }
    }

    bool Register()
    {
        try {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || registered.load()) return registered.load();
            // Never replace a menu registered by another DLL or install a hook.
            {
                RE::BSAutoReadLock guard(RE::UI::GetMenuMapRWLock());
                if (ui->menuMap.find(RE::BSFixedString(kMenu)) != ui->menuMap.end()) return false;
            }
            ui->RegisterMenu(kMenu, Create);
            registered.store(true);
            return true;
        } catch (...) { return false; }
    }

    bool Available() { return registered.load(); }

    bool Open(const InputService::View& view)
    {
        try {
            const auto* tasks = F4SE::GetTaskInterface();
            if (!registered.load() || !tasks || tasks->Version() < F4SE::TaskInterface::kVersion) return false;
            tasks->AddUITask([view]() {
                try {
                    if (!lease.empty()) { InputService::Failed(view.token, "ui-owned-menu-busy"); return; }
                    if (!InputService::Opening(view.token)) return;
                    lease = view.token; pending = view; closing = false; hideQueued = false;
                    if (!QueueMessage(RE::UI_MESSAGE_TYPE::kShow, view.token)) {
                        InputService::Failed(view.token, "ui-message-queue-unavailable");
                        ReleaseLease(view.token);
                    }
                } catch (...) {
                    InputService::Failed(view.token, "ui-dispatch-exception");
                    ReleaseLease(view.token);
                }
            });
            return true;
        } catch (...) { return false; }
    }

    bool Close(std::string_view token)
    {
        try {
            const auto* tasks = F4SE::GetTaskInterface();
            if (!tasks || tasks->Version() < F4SE::TaskInterface::kVersion) return false;
            tasks->AddUITask([token = std::string(token)]() {
                try {
                    if (lease != token) { InputService::Closed(token); return; }
                    closing = true;
                    // If Show is still queued, Create/OnAdded will request Hide.
                    const auto* ui = RE::UI::GetSingleton();
                    if (ui && ui->GetMenu(RE::BSFixedString(kMenu))) RequestHide(token);
                } catch (...) { InputService::Failed(token, "ui-close-exception"); }
            });
            return true;
        } catch (...) { return false; }
    }
}

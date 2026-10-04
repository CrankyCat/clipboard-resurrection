// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

namespace Clipboard::InputEventABI
{
    // The engine input methods are entered through IMenu's secondary
    // BSInputEventUser base. A full-menu pointer addresses a different vtable
    // and places every engine field read 0x10 bytes early. Keep the explicit
    // C++ base conversion at the common production/test dispatch boundary;
    // never use IMenu member-function wrappers for these two engine entries.
    template <class InputReceiver, class Menu, class Event, class Function>
    decltype(auto) Forward(Menu* menu, const Event* event, Function&& function)
    {
        return std::forward<Function>(function)(static_cast<InputReceiver*>(menu), event);
    }
}

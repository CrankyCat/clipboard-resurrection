// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ClipboardInputService.h"
#include <string_view>

namespace Clipboard::InputUI
{
    bool Register();
    bool Available();
    // Accepted here means queued on the UI lane, not ready for input. Opening
    // and Ready callbacks acknowledge the actual ownership and asset handshake.
    bool Open(const InputService::View& view);
    bool Close(std::string_view token);
}

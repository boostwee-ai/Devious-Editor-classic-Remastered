#pragma once

#include <cstdint>
#include <string>

namespace Packets {
    enum class MessageType : uint8_t {
        DiscoveryRequest = 0,
        DiscoveryResponse = 1,
        CollabRequest = 2,
        CollabResponse = 3,
        CursorMove = 4,
        ObjectPlace = 5,
        ObjectDelete = 6
    };

    enum class Platform : uint8_t {
        Windows = 0,
        MacOS = 1,
        Unknown = 2
    };

    inline Platform getCurrentPlatform() {
#ifdef CURRENT_OS_WINDOWS
        return Platform::Windows;
#elif defined(CURRENT_OS_MACOS)
        return Platform::MacOS;
#else
        return Platform::Unknown;
#endif
    }
}

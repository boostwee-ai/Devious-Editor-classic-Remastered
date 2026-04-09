#pragma once

#include <cstdint>
#include <string>

namespace Packets {

    enum class MessageType : uint8_t {
        // ── UDP broadcast ────────────────────────────────────────────────────
        DiscoveryRequest  = 0,
        DiscoveryResponse = 1,
        // ── UDP unicast (invite flow) ────────────────────────────────────────
        CollabRequest     = 2,
        CollabResponse    = 3,
        // ── UDP unicast (real-time session) ──────────────────────────────────
        CursorMove        = 4,  // viewport center broadcast every ~150 ms
        ObjectPlace       = 5,  // object placed in editor
        ObjectDelete      = 6,  // object deleted from editor
        LevelSettings     = 7,  // BG / ground / speed / gamemode / platformer
        SessionEnd        = 8,  // guest leaving the editor
        // ── TCP only (large payloads) ────────────────────────────────────────
        LevelInit         = 9,  // host → guest: full initial level dump
    };

    enum class Platform : uint8_t {
        Windows = 0,
        MacOS   = 1,
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

} // namespace Packets

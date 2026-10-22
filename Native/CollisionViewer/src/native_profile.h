#pragma once

#include <cstdint>

// Read-only executable fingerprint for the DOOM Eternal 1.1 layout used by
// game_camera.h. No CVar setters or memory-writing helpers are included.
constexpr uintptr_t kSetStringRva = 0x339b10;
constexpr unsigned char kSetStringBytes[] =
    {0x48, 0x89, 0x5c, 0x24, 0x8, 0x57, 0x48, 0x83, 0xec, 0x20};

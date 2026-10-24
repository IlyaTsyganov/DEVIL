#pragma once

#include "game_memory.h"
#include <initializer_list>

// Audited local retail EXEs: PE metadata + code fingerprint identify a layout.
// 1.0 SHA256: 71aacc8f2185a5ffd54dc49b2196ca366aaac3cdc509fb32d6a9219268c9ac88
// 1.1 SHA256: 0fa3177e7f74a5fe7571e1f2954db649dc99a1c8c6f6050a2a39ef799d37ea7f
// Shared Havok/entity/render-view members were compared in both EXEs.
// Globals, vtables and idPlayer::playerHud differ. Game memory is read only.
struct GameProfile {
    const char* name;
    uint32_t timestamp,imageSize;
    uintptr_t fingerprintRva,rendererRva,gameSystemRva,playerVtableRva,wallVtableRva;
    uintptr_t playerHudOffset,consoleRva,consoleVtableRva;
};
static constexpr GameProfile kProfile10{
    "DOOM Eternal 1.0 (Release)",0x5e794131,0x1e706000,
    0x336210,0x5be4c30,0x3395498,0x292a180,0x279dcb8,
    0x36270,0x409a350,0x25b4800
};
static constexpr GameProfile kProfile11{
    "DOOM Eternal 1.1",0x5ec7deca,0x1d551000,
    0x339b10,0x5bd6540,0x33fc468,0x2984578,0x27f2b58,
    0x36020,0x410bc50,0x26080b8
};
static constexpr unsigned char kProfileCode[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
static const GameProfile* detectGameProfile(const WallMemory& memory,uintptr_t base) {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!memory.get(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x100000||
       !memory.get(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    for(const GameProfile* profile:{&kProfile10,&kProfile11}) {
        if(nt.FileHeader.TimeDateStamp!=profile->timestamp||nt.OptionalHeader.SizeOfImage!=profile->imageSize) continue;
        unsigned char code[sizeof(kProfileCode)]{};
        if(memory.read(base+profile->fingerprintRva,code,sizeof(code))&&!memcmp(code,kProfileCode,sizeof(code))) return profile;
    }
    return nullptr;
}
static const GameProfile* currentGameProfile() {
    static const GameProfile* profile=detectGameProfile(WallMemory{},reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
    return profile;
}

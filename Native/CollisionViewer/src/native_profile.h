#pragma once

#include "game_memory.h"
#include <initializer_list>

// Audited local retail EXEs: PE metadata + code fingerprint identify a layout.
// 1.0 SHA256: 71aacc8f2185a5ffd54dc49b2196ca366aaac3cdc509fb32d6a9219268c9ac88
// 1.1 SHA256: 0fa3177e7f74a5fe7571e1f2954db649dc99a1c8c6f6050a2a39ef799d37ea7f
// Version-specific renderer/map/HUD members are kept in the profile.
// Entity and Havok shape members are shared. Game memory is read only.
struct GameProfile {
    const char* name;
    uint32_t timestamp,imageSize;
    uintptr_t fingerprintRva,rendererRva,gameSystemRva,playerVtableRva,wallVtableRva;
    uintptr_t playerHudOffset,consoleRva,consoleVtableRva;
    uintptr_t viewsOffset=0x5a7188,viewPositionOffset=0x20a28,viewMatrixOffset=0x213f0;
    uintptr_t mapPlayerOffset=0x1ad0,mapEntitiesOffset=0x61cb0;
    uintptr_t hudMenusOffset=0x358,hudPausedOffset=0x3ac;
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
// Retail 3.1 SHA256: 0a3c47138c785f8d6cecfaa2b0268b4233bb31a1624dee3d4ba2b317f4060afd
static constexpr GameProfile kProfile31{
    "DOOM Eternal 3.1",0x5f90c129,0x1e0c1000,
    0x316270,0x61f1690,0x35e32b8,0x2b516f0,0x29b0308,
    0x374e8,0x4651850,0x27b7ef8,
    0x5a7198,0x20a44,0x21410,0x1af8,0x61cd8,0x370,0x3c4
};
// Retail 5.1 SHA256: 93da12b964438b8bcabeb8fb12ff4050cbc6042f723b00baa89544482292594c
// Renderer/map/HUD offsets differ; entity and Havok layouts remain shared.
static constexpr GameProfile kProfile51{
    "DOOM Eternal 5.1",0x607e69b0,0x1b5dd000,
    0x31f940,0x5ea7cd0,0x367c8c0,0x2bc5628,0x2a14ec8,
    0x46cd8,0x427bb08,0x2811058,
    0x5a7198,0x20a54,0x21430,0x1af8,0x61cd8,0x388,0x3dc
};
static constexpr unsigned char kProfileCode[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
static const GameProfile* detectGameProfile(const WallMemory& memory,uintptr_t base) {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!memory.get(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x100000||
       !memory.get(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    for(const GameProfile* profile:{&kProfile10,&kProfile11,&kProfile31,&kProfile51}) {
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

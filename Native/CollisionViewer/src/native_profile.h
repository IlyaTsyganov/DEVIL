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
    uintptr_t typeGetterLimitRva=0x2500000;
    bool consoleIndirect=false; // Newer profiles use the engine's console pointer.
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
// d5e55036101b41f1ce5f1ff6c201f70a21c7d84a6f151a4f4799f8eb8564cac4
static constexpr GameProfile kProfile20{
    "DOOM Eternal 2.0",0x5eeb0267,0x1d396000,
    0x303dc0,0x5f55ae0,0x3465700,0x29dd7f0,0x2848358,
    0x36e18,0x44848a0,0x2658d48,
    0x5a7188,0x20a28,0x213f0,0x1ad8,0x61cb8,0x358,0x3ac,
    0x25fd600,true
};
// 7e66b8a1527689c0e6c61d277d59716cc3273e6bf21f98e8bd6f5d0b245407dd
static constexpr GameProfile kProfile21{
    "DOOM Eternal 2.1",0x5f203d59,0x1e1ef000,
    0x304520,0x5f589e0,0x3468708,0x29dfe78,0x2849948,
    0x36a30,0x4487920,0x2659e08,
    0x5a7188,0x20a28,0x213f0,0x1ad8,0x61cb8,0x358,0x3ac,
    0x25fea00,true
};
// bb5b6942b0cfe237486e802cce536de7f3d8a8219d7d3caf7f5f97ec590f0a6a
static constexpr GameProfile kProfile30{
    "DOOM Eternal 3.0",0x5f8065da,0x1c587000,
    0x316270,0x61f1610,0x35e32b8,0x2b516e0,0x29b02f8,
    0x374e8,0x464b998,0x27b7ef8,
    0x5a7198,0x20a44,0x21410,0x1af8,0x61cd8,0x370,0x3c4,
    0x275a800,true
};
// Retail 3.1 SHA256: 0a3c47138c785f8d6cecfaa2b0268b4233bb31a1624dee3d4ba2b317f4060afd
static constexpr GameProfile kProfile31{
    "DOOM Eternal 3.1",0x5f90c129,0x1e0c1000,
    0x316270,0x61f1690,0x35e32b8,0x2b516f0,0x29b0308,
    0x374e8,0x4651850,0x27b7ef8,
    0x5a7198,0x20a44,0x21410,0x1af8,0x61cd8,0x370,0x3c4
};
// 7bea7ebd1e42dedf4a48c27c86c8a5d78a4e5d71957c6c42013094d6fb287038
static constexpr GameProfile kProfile40{
    "DOOM Eternal 4.0",0x5fb68f01,0x1c7e9000,
    0x3172c0,0x61aefb0,0x3575148,0x2aecde8,0x2948700,
    0x373e0,0x45db398,0x274f3f8,
    0x5a7198,0x20a44,0x21410,0x1af8,0x61cd8,0x370,0x3c4,
    0x26f1400,true
};
// 911482009da6d0b78e8445402e85a6bdf4b19ac3a54319d5c67e4e1b2eaad874
static constexpr GameProfile kProfile41{
    "DOOM Eternal 4.1",0x5fc9da94,0x1c2eb000,
    0x317360,0x61aefb0,0x3575148,0x2aecee0,0x29487f0,
    0x373e0,0x45db398,0x274f3f8,
    0x5a7198,0x20a44,0x21410,0x1af8,0x61cd8,0x370,0x3c4,
    0x26f1e00,true
};
// 9848ea9bcba71687fff215e3bfe86457eccd91c5965da1fad51e1ba4c1f85b39
static constexpr GameProfile kProfile50{
    "DOOM Eternal 5.0",0x603896f3,0x1c5bf000,
    0x31f940,0x5ea7cd0,0x367c8c0,0x2bc5628,0x2a14ec8,
    0x46cd8,0x4275c18,0x2811058,
    0x5a7198,0x20a54,0x21430,0x1af8,0x61cd8,0x388,0x3dc,
    0x27b2200,true
};
// Retail 5.1 SHA256: 93da12b964438b8bcabeb8fb12ff4050cbc6042f723b00baa89544482292594c
// Renderer/map/HUD offsets differ; entity and Havok layouts remain shared.
static constexpr GameProfile kProfile51{
    "DOOM Eternal 5.1",0x607e69b0,0x1b5dd000,
    0x31f940,0x5ea7cd0,0x367c8c0,0x2bc5628,0x2a14ec8,
    0x46cd8,0x427bb08,0x2811058,
    0x5a7198,0x20a54,0x21430,0x1af8,0x61cd8,0x388,0x3dc
};
// 7abfc3fda951723636863d5722f266d31bb3236488c1e97df1f4301129d39d16
static constexpr GameProfile kProfile60{
    "DOOM Eternal 6.0",0x60c98dd6,0x1cb22000,
    0x31f050,0x6287920,0x383b068,0x2d1ee78,0x2b6c998,
    0x46da8,0x4378518,0x2969548,
    0x5a71a8,0x28a64,0x29440,0x1af8,0x61cd8,0x388,0x3dc,
    0x2909000,true
};
// 1f972ea4cc13a3bfcba01577ca8fd4f3c35c6aec01cfc9468135dd8d19fe2031
static constexpr GameProfile kProfile61{
    "DOOM Eternal 6.1",0x60dff532,0x1bc55000,
    0x31f050,0x6287920,0x383b068,0x2d1ee78,0x2b6c998,
    0x46da8,0x4378518,0x2969548,
    0x5a71a8,0x28a64,0x29440,0x1af8,0x61cd8,0x388,0x3dc,
    0x2909000,true
};
// 3f86b9d3a0a3be5f2aedc8e22576c7787d6b93459d1a0bcaab5991fff34ea4f9
static constexpr GameProfile kProfile62{
    "DOOM Eternal 6.2",0x60f26b2f,0x1bb06000,
    0x31f050,0x6289ea0,0x383d068,0x2d1f978,0x2b6ce68,
    0x46da8,0x437aa98,0x2969548,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x388,0x3dc,
    0x2909000,true
};
// be22cb6003c12b059ad02fd865208008f2f0489070098312b72f1ebe6b510930
static constexpr GameProfile kProfile63{
    "DOOM Eternal 6.3",0x6116d6b5,0x1cd60000,
    0x31f050,0x6289ea0,0x383d068,0x2d1f978,0x2b6ce58,
    0x46da8,0x437aa98,0x2969548,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x388,0x3dc,
    0x2909000,true
};
// b5b27d2b023af52c12cbe0b5dbac47b9eea1072406932a0cff83396782993747
static constexpr GameProfile kProfile64{
    "DOOM Eternal 6.4",0x61202c76,0x1d77e000,
    0x31f050,0x6289ea0,0x383d068,0x2d1f968,0x2b6ce48,
    0x46da8,0x437aa98,0x2969548,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x388,0x3dc,
    0x2909000,true
};
// 6.66 revisions: PE identity and offsets audited separately in each local EXE.
// 4a6c7962562462e83b99d490412c23a6054972df15ff3f09f05c2193cbe2874f
static constexpr GameProfile kProfile6660{
    "DOOM Eternal 6.66",0x61692c83,0x1e4cd000,
    0x32fa00,0x6400940,0x3969438,0x2e0abd0,0x2c48728,
    0x472d8,0x44fb498,0x2a426d0,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x390,0x3e4,
    0x29e8a00,true
};
// cb1f3a25c34ddee1878fb206c0ddb7508f6d0ddad4db272bd58bb8cacf93075f
static constexpr GameProfile kProfile6661{
    "DOOM Eternal 6.66 Rev 1",0x618dfc01,0x1c835000,
    0x32fac0,0x6423fc0,0x398c438,0x2e2ae68,0x2c68a88,
    0x472d8,0x451eb18,0x2a5d6d0,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x390,0x3e4,
    0x2a03400,true
};
// c7cf6702b5602a25f0969f748b72849e674b4f0c4cb898b102b6169d5fc4d26d
static constexpr GameProfile kProfile66611{
    "DOOM Eternal 6.66 Rev 1.1",0x61d7704e,0x1c58a000,
    0x32fcb0,0x64253c0,0x398d438,0x2e2bf68,0x2c69ad8,
    0x472d8,0x451ff18,0x2a5e710,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x390,0x3e4,
    0x2a04c00,true
};
// 84d6d3d01855fa5900f7ff8e452a14d9316ebc459bafdc4c4480831b537ff375
static constexpr GameProfile kProfile6662{
    "DOOM Eternal 6.66 Rev 2",0x6256614c,0x1e69d000,
    0x331e50,0x6460140,0x39c3498,0x2e5b930,0x2c97ff8,
    0x472e8,0x455ab18,0x2a8b4f8,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x398,0x3ec,
    0x2a31c00,true
};
// 7352dc4da0b107939c7d4d1e2c42ea087c566c26e79f8e01270678a0c61f6b39
static constexpr GameProfile kProfile66622{
    "DOOM Eternal 6.66 Rev 2.2",0x64cc8a9b,0x7421000,
    0x331980,0x64340c0,0x399b498,0x2e38d10,0x2c75fc8,
    0x472d0,0x452ea98,0x2a694d8,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x398,0x3ec,
    0x2a0fb3d,true
};
// 005f887a04450ab3f34535be658a711a8fcd3fbb602d07bec19b54ecbf53efa8
static constexpr GameProfile kProfile6663{
    "DOOM Eternal 6.66 Rev 3",0x669f0dcf,0x744d000,
    0x331160,0x66d08a0,0x3880398,0x2da6078,0x2be21f0,
    0x472d0,0x47cb218,0x2e3c928,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x398,0x3ec,
    0x2a0c34d,true
};
// 69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11
static constexpr GameProfile kProfile66632{
    "DOOM Eternal 6.66 Rev 3.2",0x6a7b9b8c,0x7431000,
    0x335b70,0x66e2d60,0x38913a0,0x2db5698,0x2bf12f0,
    0x472d0,0x47dd918,0x2e4c840,
    0x5e71c0,0x28a64,0x29440,0x1af8,0x61cd8,0x398,0x3ec,
    0x2a1ab5d,true
};
static constexpr const GameProfile* kGameProfiles[]{
    &kProfile10,&kProfile11,&kProfile20,&kProfile21,&kProfile30,&kProfile31,
    &kProfile40,&kProfile41,&kProfile50,&kProfile51,
    &kProfile60,&kProfile61,&kProfile62,&kProfile63,&kProfile64,
    &kProfile6660,&kProfile6661,&kProfile66611,&kProfile6662,&kProfile66622,&kProfile6663,&kProfile66632
};
static constexpr unsigned char kProfileCode[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
static const GameProfile* detectGameProfile(const WallMemory& memory,uintptr_t base) {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!memory.get(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0x40||dos.e_lfanew>0x100000||
       !memory.get(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    for(const GameProfile* profile:kGameProfiles) {
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

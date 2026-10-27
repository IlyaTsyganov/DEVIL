#pragma once
#include <cstddef>
#include <cstdint>

// Read-only ABI view of mh_interface_t through m_register_premainloop_cb.
// Source: brongo/m3337ho0o0ok/m34thook/mh_native_api.hpp (v7.2).
// Unused slots include the adjacent sizeof_generalized_number/pad uint32s.
// No meathook implementation code is incorporated here.
struct MeathookHudApi {
    void (*executeCommand)(const char*);
    void (*consolePrint)(const char*,...);
    uintptr_t unused0[5];
    void* (*findClass)(const char*);
    uintptr_t unused1[16];
    const char* (*entityName)(void*);
    void (*addText)(unsigned,unsigned,unsigned,float,const char*,...);
    void (*removeText)(unsigned,unsigned);
    uintptr_t unused2[2];
    void (*registerPreFrame)(void (*)(void*),void*);
};
static_assert(offsetof(MeathookHudApi,executeCommand)==0,"meathook x64 command ABI");
static_assert(offsetof(MeathookHudApi,findClass)==56,"meathook x64 class ABI");
static_assert(offsetof(MeathookHudApi,entityName)==192,"meathook x64 name ABI");
static_assert(offsetof(MeathookHudApi,addText)==200,"meathook x64 HUD ABI");
static_assert(offsetof(MeathookHudApi,registerPreFrame)==232,"meathook x64 callback ABI");

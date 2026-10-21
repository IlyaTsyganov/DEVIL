#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "native_profile.h"

struct GameCamera { float matrix[16]; float position[3]; };

// Layouts are recovered from SDK/1.1 reflection, not guessed offsets from newer builds.
static bool readGameCamera(GameCamera& camera) {
    __try {
        uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(memcmp(reinterpret_cast<void*>(base+kSetStringRva),kSetStringBytes,sizeof(kSetStringBytes))!=0) return false;
        uintptr_t renderer=*reinterpret_cast<uintptr_t*>(base+0x5bd6540);
        if(!renderer) return false;
        int count=*reinterpret_cast<int*>(renderer+0x5a7190);
        uintptr_t* views=*reinterpret_cast<uintptr_t**>(renderer+0x5a7188);
        if(count<1 || count>32 || !views || !views[0]) return false;
        uintptr_t view=views[0];
        memcpy(camera.position,reinterpret_cast<void*>(view+0x20a28),12);
        memcpy(camera.matrix,reinterpret_cast<void*>(view+0x213f0),64);
        for(float value:camera.matrix) if(!std::isfinite(value)) return false;
        for(float value:camera.position) if(!std::isfinite(value)) return false;
        float cameraW=camera.matrix[12]*camera.position[0]+camera.matrix[13]*camera.position[1]+camera.matrix[14]*camera.position[2]+camera.matrix[15];
        float forwardLength=camera.matrix[12]*camera.matrix[12]+camera.matrix[13]*camera.matrix[13]+camera.matrix[14]*camera.matrix[14];
        return fabsf(cameraW)<0.5f && forwardLength>0.9f && forwardLength<1.1f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

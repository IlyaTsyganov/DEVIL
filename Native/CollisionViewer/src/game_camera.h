#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "native_profile.h"

struct GameCamera { float matrix[16]; float position[3]; };

// Both retail versions share these renderer/view member layouts.
static bool readGameCamera(const WallMemory& memory,uintptr_t base,const GameProfile& profile,GameCamera& camera) {
    uintptr_t renderer=0,views=0,view=0; int count=0;
    if(!memory.get(base+profile.rendererRva,renderer)||!renderer||
       !memory.get(renderer+0x5a7190,count)||!memory.get(renderer+0x5a7188,views)||
       count<1||count>32||!views||!memory.get(views,view)||!view) return false;
    if(!memory.read(view+0x20a28,camera.position,12)||!memory.read(view+0x213f0,camera.matrix,64)) return false;
    for(float value:camera.matrix) if(!std::isfinite(value)) return false;
    for(float value:camera.position) if(!std::isfinite(value)) return false;
    float cameraW=camera.matrix[12]*camera.position[0]+camera.matrix[13]*camera.position[1]+camera.matrix[14]*camera.position[2]+camera.matrix[15];
    float forwardLength=camera.matrix[12]*camera.matrix[12]+camera.matrix[13]*camera.matrix[13]+camera.matrix[14]*camera.matrix[14];
    return fabsf(cameraW)<0.5f && forwardLength>0.9f && forwardLength<1.1f;
}

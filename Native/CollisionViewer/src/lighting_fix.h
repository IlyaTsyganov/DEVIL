#pragma once

// Adapted from billymooreq's DoomEternal-v1.1--RTXFix, default "on" mode.
// Upstream commit: 0bf1f44b5f4945eb7e6b76a204b4f6f2a074e23e.
// Copyright (c) 2026 billymooreq. MIT license: ../licenses/doomfix-MIT.txt.
// Only the Vulkan 1.x barrier APIs used by these three old builds are needed.
// The overlay is submitted separately at presentation, after the game's work.

static bool lightingFixEligible(const GameProfile* profile,uint32_t vendor,bool requested) {
    return requested&&vendor==0x10de&&
        (profile==&kProfile10||profile==&kProfile11||profile==&kProfile20);
}

static bool lightingFixRequested() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"DEVIL_LIGHTING_FIX",value,2)==1&&value[0]==L'1';
}

static VkAccessFlags lightingWritesForStages(VkPipelineStageFlags stages) {
    if(stages&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) return VK_ACCESS_MEMORY_WRITE_BIT;
    constexpr VkPipelineStageFlags shaders=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|
        VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT|VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT|
        VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    VkAccessFlags writes=0;
    if(stages&shaders) writes|=VK_ACCESS_SHADER_WRITE_BIT;
    if(stages&VK_PIPELINE_STAGE_TRANSFER_BIT) writes|=VK_ACCESS_TRANSFER_WRITE_BIT;
    if(stages&VK_PIPELINE_STAGE_HOST_BIT) writes|=VK_ACCESS_HOST_WRITE_BIT;
    return writes;
}

struct LightingBarrierPatch {
    uint32_t memoryCount;
    const VkMemoryBarrier* memory;
    const VkBufferMemoryBarrier* buffers;
    bool changed=false;
    std::vector<VkMemoryBarrier> memoryStorage;
    std::vector<VkBufferMemoryBarrier> bufferStorage;

    LightingBarrierPatch(bool active,VkPipelineStageFlags src,VkPipelineStageFlags dst,
                         uint32_t count,const VkMemoryBarrier* inputMemory,
                         uint32_t bufferCount,const VkBufferMemoryBarrier* inputBuffers)
        :memoryCount(count),memory(inputMemory),buffers(inputBuffers) {
        if(!active||!(dst&VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)) return;
        if(!count&&!bufferCount) {
            memoryStorage.push_back({VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,
                lightingWritesForStages(src),VK_ACCESS_INDIRECT_COMMAND_READ_BIT});
            memoryCount=1; memory=memoryStorage.data(); changed=true;
            return;
        }
        constexpr VkAccessFlags covered=VK_ACCESS_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_MEMORY_READ_BIT;
        bool fixMemory=false,fixBuffers=false;
        for(uint32_t i=0;i<count;++i) fixMemory|=(inputMemory[i].dstAccessMask&covered)==0;
        for(uint32_t i=0;i<bufferCount;++i) fixBuffers|=(inputBuffers[i].dstAccessMask&covered)==0;
        if(fixMemory) {
            memoryStorage.assign(inputMemory,inputMemory+count);
            for(auto& barrier:memoryStorage) barrier.dstAccessMask|=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            memory=memoryStorage.data(); changed=true;
        }
        if(fixBuffers) {
            bufferStorage.assign(inputBuffers,inputBuffers+bufferCount);
            for(auto& barrier:bufferStorage) barrier.dstAccessMask|=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            buffers=bufferStorage.data(); changed=true;
        }
    }
};

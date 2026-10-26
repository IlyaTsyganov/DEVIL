#include "../src/vulkan_layer.cpp"

static void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static uint32_t forwarded=0;
static const VkMemoryBarrier* originalMemory=nullptr;
static const VkBufferMemoryBarrier* originalBuffers=nullptr;
static const VkImageMemoryBarrier* originalImages=nullptr;
static VkEvent testEvent=reinterpret_cast<VkEvent>(uintptr_t(50));
static bool expectPatch=true;
static VKAPI_ATTR void VKAPI_CALL capture(VkCommandBuffer,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t count,const VkMemoryBarrier* memory,uint32_t bufferCount,const VkBufferMemoryBarrier* buffers,uint32_t imageCount,const VkImageMemoryBarrier* images) {
    ++forwarded;
    check(src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&dst==(VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),"Stage masks changed");
    check(flags==VK_DEPENDENCY_BY_REGION_BIT&&imageCount==1&&images==originalImages,"Image dependencies or flags changed");
    check(count==1&&bufferCount==1,"Barrier counts changed");
    if(expectPatch) {
        check(memory!=originalMemory&&buffers!=originalBuffers,"Caller arrays were edited in place");
        check(memory[0].dstAccessMask==(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT),"Memory barrier not fixed before forwarding");
        check(buffers[0].dstAccessMask==(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT),"Buffer barrier not fixed before forwarding");
    } else check(memory==originalMemory&&buffers==originalBuffers,"Disabled fix copied or changed barriers");
    check(memory[0].srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&memory[0].pNext==originalMemory[0].pNext,"Source access or pNext changed");
    check(buffers[0].buffer==originalBuffers[0].buffer&&buffers[0].offset==64&&buffers[0].size==128&&buffers[0].srcQueueFamilyIndex==7&&buffers[0].dstQueueFamilyIndex==9,"Buffer ranges or queue ownership changed");
}
static VKAPI_ATTR void VKAPI_CALL captureWait(VkCommandBuffer command,uint32_t count,const VkEvent* events,VkPipelineStageFlags src,VkPipelineStageFlags dst,uint32_t memoryCount,const VkMemoryBarrier* memory,uint32_t bufferCount,const VkBufferMemoryBarrier* buffers,uint32_t imageCount,const VkImageMemoryBarrier* images) {
    check(count==1&&events[0]==testEvent,"Wait events changed");
    capture(command,src,dst,VK_DEPENDENCY_BY_REGION_BIT,memoryCount,memory,bufferCount,buffers,imageCount,images);
}
int main() {
    try {
        for(const GameProfile* p:kGameProfiles) {
            const bool expected=p==&kProfile10||p==&kProfile11||p==&kProfile20;
            check(lightingFixEligible(p,0x10de,true)==expected,"Wrong game version eligible");
            check(!lightingFixEligible(p,0x1002,true)&&!lightingFixEligible(p,0x8086,true),"Non-NVIDIA device eligible");
            check(!lightingFixEligible(p,0x10de,false),"Manual launch opted in");
        }
        check(!lightingFixEligible(nullptr,0x10de,true),"Unknown executable eligible");
        VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        b.buffer=reinterpret_cast<VkBuffer>(uintptr_t(10)); b.offset=64; b.size=128; b.srcQueueFamilyIndex=7; b.dstQueueFamilyIndex=9;
        VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; image.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        originalMemory=&m; originalBuffers=&b; originalImages=&image;
        // Exercise the actual exported hooks with a fake downstream Vulkan driver.
        void* dispatchKey=reinterpret_cast<void*>(uintptr_t(100));
        VkCommandBuffer command=reinterpret_cast<VkCommandBuffer>(&dispatchKey);
        auto device=std::make_unique<Device>(); device->lightingFix=true; device->CmdPipelineBarrier=capture; device->CmdWaitEvents=captureWait;
        Device* d=device.get(); devices[dispatchKey]=std::move(device);
        enabled=false; // The fix must work even with the collision viewer off.
        const auto src=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        const VkPipelineStageFlags dst=VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        vkCmdPipelineBarrier(command,src,dst,VK_DEPENDENCY_BY_REGION_BIT,1,&m,1,&b,1,&image);
        vkCmdWaitEvents(command,1,&testEvent,src,dst,1,&m,1,&b,1,&image);
        check(d->lightingPatched==2&&forwarded==2,"Actual hooks bypassed correction");
        check(m.dstAccessMask==VK_ACCESS_SHADER_READ_BIT&&b.dstAccessMask==VK_ACCESS_SHADER_READ_BIT,"Caller data mutated");
        expectPatch=false; d->lightingFix=false;
        vkCmdPipelineBarrier(command,src,dst,VK_DEPENDENCY_BY_REGION_BIT,1,&m,1,&b,1,&image);
        check(d->lightingPatched==2&&forwarded==3,"Disabled hook did not forward unchanged");
        LightingBarrierPatch unrelated(true,src,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,1,&m,1,&b);
        check(!unrelated.changed&&unrelated.memory==&m&&unrelated.buffers==&b,"Unrelated barrier modified");
        m.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT; b.dstAccessMask=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        LightingBarrierPatch alreadyFixed(true,src,dst,1,&m,1,&b);
        check(!alreadyFixed.changed&&alreadyFixed.memory==&m&&alreadyFixed.buffers==&b,"Existing upstream fix not idempotent");
        LightingBarrierPatch empty(true,src,dst,0,nullptr,0,nullptr);
        check(empty.changed&&empty.memoryCount==1&&empty.memory[0].srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&empty.memory[0].dstAccessMask==VK_ACCESS_INDIRECT_COMMAND_READ_BIT,"Missing global dependency not added");
        LightingBarrierPatch repeated(true,src,dst,empty.memoryCount,empty.memory,0,nullptr);
        check(!repeated.changed,"Added dependency not idempotent");
        check(lightingWritesForStages(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)==VK_ACCESS_MEMORY_WRITE_BIT,"ALL_COMMANDS source wrong");
        check(lightingWritesForStages(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT)==(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT),"Source stages not respected");
        devices.clear();
        puts("PASS: 1.0/1.1/2.0 NVIDIA launch gating, real barrier/event forwarding, immutable inputs, idempotence, fallback and viewer-off operation.");
        return 0;
    } catch(const std::exception& error) { fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}

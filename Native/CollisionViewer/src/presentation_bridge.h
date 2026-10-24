#pragma once

// A presentation queue need not support graphics. Preserve the game's sharing
// mode; exclusive swapchains use paired ownership transfers in both directions.
static bool graphicsFamilyAllowed(const Swapchain& swap,uint32_t family) {
    return swap.sharing==VK_SHARING_MODE_EXCLUSIVE||
        std::find(swap.sharingFamilies.begin(),swap.sharingFamilies.end(),family)!=swap.sharingFamilies.end();
}
static VkQueue overlayQueue(Swapchain& swap,VkQueue presentQueue,uint32_t presentFamily,uint32_t& family) {
    Device& d=*swap.device;
    auto graphics=[&](uint32_t candidate) { return candidate<d.families.size()&&(d.families[candidate].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&graphicsFamilyAllowed(swap,candidate); };
    if((!swap.ready||swap.family==presentFamily)&&graphics(presentFamily)) { family=presentFamily; return presentQueue; }
    VkQueue selected{}; family=~0u;
    for(const auto& pair:queues) {
        if(pair.second.device!=&d||!graphics(pair.second.family)||(swap.ready&&pair.second.family!=swap.family)) continue;
        if(!selected||pair.second.family<family) { selected=pair.first; family=pair.second.family; }
    }
    return selected;
}
static void swapOwnershipBarrier(Device& d,VkCommandBuffer command,VkImage image,uint32_t from,uint32_t to,bool acquire,bool graphics) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image=image; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex=from; barrier.dstQueueFamilyIndex=to;
    if(graphics) {
        if(acquire) barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        else barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    VkPipelineStageFlags source=acquire?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:(graphics?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkPipelineStageFlags destination=acquire&&graphics?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    d.CmdPipelineBarrier(command,source,destination,0,0,nullptr,0,nullptr,1,&barrier);
}
static void preparePresentHandoff(Swapchain& swap,Frame& frame,uint32_t presentFamily) {
    Device& d=*swap.device;
    // Called after this image's completion fence; none of its bridge commands
    // are still in flight. Semaphores are indexed by acquired swapchain image.
    if(frame.handoffPool&&frame.handoffFamily!=presentFamily) {
        d.DestroyCommandPool(d.handle,frame.handoffPool,nullptr); frame.handoffPool={};
    }
    if(!frame.handoffPool) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex=presentFamily; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require(d.CreateCommandPool(d.handle,&pool,nullptr,&frame.handoffPool),"Create presentation handoff pool failed"); frame.handoffFamily=presentFamily;
        VkCommandBuffer commands[2]{}; VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool=frame.handoffPool; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount=2;
        require(d.AllocateCommandBuffers(d.handle,&allocate,commands),"Allocate presentation handoff commands failed");
        frame.releaseToGraphics=commands[0]; frame.acquireForPresent=commands[1];
        for(VkCommandBuffer command:commands) if(d.setLoaderData) require(d.setLoaderData(d.handle,command),"Set presentation command loader data failed");
    }
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if(!frame.graphicsReady) require(d.CreateSemaphore(d.handle,&semaphore,nullptr,&frame.graphicsReady),"Create graphics handoff semaphore failed");
    if(!frame.presentReady) require(d.CreateSemaphore(d.handle,&semaphore,nullptr,&frame.presentReady),"Create presentation handoff semaphore failed");
    for(bool acquire:{false,true}) {
        VkCommandBuffer command=acquire?frame.acquireForPresent:frame.releaseToGraphics;
        require(d.ResetCommandBuffer(command,0),"Reset presentation handoff command failed");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(d.BeginCommandBuffer(command,&begin),"Begin presentation handoff command failed");
        swapOwnershipBarrier(d,command,frame.image,acquire?swap.family:presentFamily,acquire?presentFamily:swap.family,acquire,false);
        require(d.EndCommandBuffer(command),"End presentation handoff command failed");
    }
}

#pragma once

static Device* commandDevice(VkCommandBuffer command) { return findDevice(reinterpret_cast<VkDevice>(command)); }
static Device* queueDevice(VkQueue queue) { return findDevice(reinterpret_cast<VkDevice>(queue)); }

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* image) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device);
    VkResult result=d.CreateImage(device,info,allocator,image);
    if(result==VK_SUCCESS&&depthFormat(info->format)) {
        DepthImage value{}; value.handle=*image; value.format=info->format; value.extent=info->extent; value.usage=info->usage;
        value.samples=info->samples; value.layers=info->arrayLayers; value.mips=info->mipLevels; value.layout=info->initialLayout;
        d.depth.images[*image]=value;
        char text[220]; sprintf_s(text,"Depth image=%p format=%d size=%ux%u samples=%u usage=0x%x layers=%u",reinterpret_cast<void*>(*image),info->format,info->extent.width,info->extent.height,info->samples,info->usage,info->arrayLayers); note(text);
    }
    return result;
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device);
    auto found=d.depth.images.find(image);
    if(found!=d.depth.images.end()) { if(found->second.sampleView) d.DestroyImageView(device,found->second.sampleView,nullptr); d.depth.images.erase(found); }
    d.DestroyImage(device,image,allocator);
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device,const VkImageViewCreateInfo* info,const VkAllocationCallbacks* allocator,VkImageView* view) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device);
    VkResult result=d.CreateImageView(device,info,allocator,view);
    if(result==VK_SUCCESS&&d.depth.images.count(info->image)) d.depth.views[*view]={info->image,info->subresourceRange};
    return result;
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device,VkImageView view,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); d.depth.views.erase(view); d.DestroyImageView(device,view,allocator);
}

template<class T> static void rememberPass(Device& d,VkRenderPass pass,const T* info) {
    DepthPass value{}; bool useful=false;
    for(uint32_t i=0;i<info->attachmentCount;++i) {
        const auto& a=info->pAttachments[i]; bool depth=depthFormat(a.format); useful|=depth;
        value.attachments.push_back({a.initialLayout,a.finalLayout,a.loadOp,a.storeOp,depth});
    }
    for(uint32_t i=0;i<info->subpassCount;++i) {
        const auto* ref=info->pSubpasses[i].pDepthStencilAttachment;
        value.subpasses.push_back(ref?ref->attachment:VK_ATTACHMENT_UNUSED);
    }
    if(useful) d.depth.passes[pass]=std::move(value);
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks* allocator,VkRenderPass* pass) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); VkResult r=d.CreateRenderPass(device,info,allocator,pass); if(r==VK_SUCCESS) rememberPass(d,*pass,info); return r;
}
#define CREATE_PASS2(name) \
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk##name(VkDevice device,const VkRenderPassCreateInfo2* info,const VkAllocationCallbacks* allocator,VkRenderPass* pass) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); VkResult r=d.name(device,info,allocator,pass); if(r==VK_SUCCESS) rememberPass(d,*pass,info); return r; }
CREATE_PASS2(CreateRenderPass2)
CREATE_PASS2(CreateRenderPass2KHR)
#undef CREATE_PASS2
EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device,VkRenderPass pass,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); d.depth.passes.erase(pass); d.DestroyRenderPass(device,pass,allocator);
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks* allocator,VkFramebuffer* framebuffer) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); VkResult r=d.CreateFramebuffer(device,info,allocator,framebuffer);
    if(r==VK_SUCCESS&&d.depth.passes.count(info->renderPass)) {
        DepthFramebuffer value{}; value.extent={info->width,info->height};
        if(info->pAttachments) value.views.assign(info->pAttachments,info->pAttachments+info->attachmentCount);
        d.depth.framebuffers[*framebuffer]=std::move(value);
    }
    return r;
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device,VkFramebuffer framebuffer,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); d.depth.framebuffers.erase(framebuffer); d.DestroyFramebuffer(device,framebuffer,allocator);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command,const VkCommandBufferBeginInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command);
    auto& state=d.depth.commands[command]; state.events.clear(); state.pass={}; state.framebuffer={}; state.subpass=0; state.viewport={}; state.temporaryViews.clear(); state.dynamicImages.clear();
    return d.BeginCommandBuffer(command,info);
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer command,VkCommandBufferResetFlags flags) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); d.depth.commands.erase(command); return d.ResetCommandBuffer(command,flags);
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*findDevice(device); for(uint32_t i=0;i<count;++i) d.depth.commands.erase(commands[i]); d.FreeCommandBuffers(device,pool,count,commands);
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer command,uint32_t first,uint32_t count,const VkViewport* viewports) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command);
    if(first==0&&count) { auto& state=d.depth.commands[command]; state.viewport=viewports[0]; DepthEvent event{}; event.kind=DepthEvent::Viewport; event.viewport=viewports[0]; state.events.push_back(event); }
    d.CmdSetViewport(command,first,count,viewports);
}
static void beginDepthPass(Device& d,VkCommandBuffer command,const VkRenderPassBeginInfo* info) {
    auto& state=d.depth.commands[command]; state.pass=info->renderPass; state.framebuffer=info->framebuffer; state.subpass=0; state.temporaryViews.clear();
    DepthEvent begin{}; begin.kind=DepthEvent::BeginPass; state.events.push_back(begin);
    for(auto p=reinterpret_cast<const VkBaseInStructure*>(info->pNext);p;p=p->pNext) if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
        const auto* attachments=reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(p);
        state.temporaryViews.assign(attachments->pAttachments,attachments->pAttachments+attachments->attachmentCount);
    }
}
static void endDepthPass(Device& d,VkCommandBuffer command) {
    auto& state=d.depth.commands[command]; auto pass=d.depth.passes.find(state.pass); auto fb=d.depth.framebuffers.find(state.framebuffer);
    if(pass!=d.depth.passes.end()&&fb!=d.depth.framebuffers.end()) {
        const auto& views=state.temporaryViews.empty()?fb->second.views:state.temporaryViews;
        for(size_t i=0;i<pass->second.attachments.size()&&i<views.size();++i) {
            const auto& attachment=pass->second.attachments[i]; if(!attachment.depth) continue;
            auto view=d.depth.views.find(views[i]); if(view==d.depth.views.end()||view->second.range.baseMipLevel||view->second.range.baseArrayLayer) continue;
            VkViewport viewport=state.viewport;
            if(viewport.width==0||viewport.height==0) viewport={0,0,static_cast<float>(fb->second.extent.width),static_cast<float>(fb->second.extent.height),0,1};
            DepthEvent use{}; use.kind=DepthEvent::Use; use.image=view->second.image; use.viewport=viewport; use.stored=attachment.store==VK_ATTACHMENT_STORE_OP_STORE; state.events.push_back(use);
            DepthEvent layout{}; layout.image=use.image; layout.layout=attachment.final; state.events.push_back(layout);
        }
    }
    state.pass={}; state.framebuffer={}; state.temporaryViews.clear();
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer command,const VkRenderPassBeginInfo* info,VkSubpassContents contents) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); beginDepthPass(d,command,info); d.CmdBeginRenderPass(command,info,contents);
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer command) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); endDepthPass(d,command); d.CmdEndRenderPass(command);
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer command,VkSubpassContents contents) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); ++d.depth.commands[command].subpass; d.CmdNextSubpass(command,contents);
}
#define BEGIN_PASS2(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command,const VkRenderPassBeginInfo* info,const VkSubpassBeginInfo* subpass) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); beginDepthPass(d,command,info); d.name(command,info,subpass); }
BEGIN_PASS2(CmdBeginRenderPass2)
BEGIN_PASS2(CmdBeginRenderPass2KHR)
#undef BEGIN_PASS2
#define END_PASS2(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command,const VkSubpassEndInfo* info) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); endDepthPass(d,command); d.name(command,info); }
END_PASS2(CmdEndRenderPass2)
END_PASS2(CmdEndRenderPass2KHR)
#undef END_PASS2
#define NEXT_PASS2(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command,const VkSubpassBeginInfo* begin,const VkSubpassEndInfo* end) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); ++d.depth.commands[command].subpass; d.name(command,begin,end); }
NEXT_PASS2(CmdNextSubpass2)
NEXT_PASS2(CmdNextSubpass2KHR)
#undef NEXT_PASS2

template<class Barrier> static void rememberBarriers(Device& d,VkCommandBuffer command,uint32_t count,const Barrier* barriers) {
    for(uint32_t i=0;i<count;++i) {
        const auto& barrier=barriers[i];
        if(!(barrier.subresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)||barrier.subresourceRange.baseMipLevel||barrier.subresourceRange.baseArrayLayer||!d.depth.images.count(barrier.image)) continue;
        DepthEvent event{}; event.image=barrier.image; event.layout=barrier.newLayout; event.owner=barrier.dstQueueFamilyIndex; d.depth.commands[command].events.push_back(event);
    }
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memoryCount,const VkMemoryBarrier* memory,uint32_t bufferCount,const VkBufferMemoryBarrier* buffers,uint32_t imageCount,const VkImageMemoryBarrier* images) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); rememberBarriers(d,command,imageCount,images); d.CmdPipelineBarrier(command,src,dst,flags,memoryCount,memory,bufferCount,buffers,imageCount,images);
}
#define BARRIER2(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command,const VkDependencyInfo* info) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); rememberBarriers(d,command,info->imageMemoryBarrierCount,info->pImageMemoryBarriers); d.name(command,info); }
BARRIER2(CmdPipelineBarrier2)
BARRIER2(CmdPipelineBarrier2KHR)
#undef BARRIER2
EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer command,uint32_t count,const VkCommandBuffer* secondary) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command);
    for(uint32_t i=0;i<count;++i) { DepthEvent event{}; event.kind=DepthEvent::Execute; event.secondary=secondary[i]; d.depth.commands[command].events.push_back(event); }
    d.CmdExecuteCommands(command,count,secondary);
}
static bool betterDepthViewport(const VkViewport& candidate,const VkViewport& previous) {
    if(!candidate.width||!candidate.height) return false;
    float range=fabsf(candidate.maxDepth-candidate.minDepth),oldRange=fabsf(previous.maxDepth-previous.minDepth);
    return !previous.width||!previous.height||range>oldRange+.001f||(fabsf(range-oldRange)<.001f&&fabsf(candidate.width*candidate.height)>fabsf(previous.width*previous.height)*1.01f);
}
struct DepthExecution { VkViewport current{},best{}; };
static void applyDepthCommands(Device& d,VkCommandBuffer command,uint32_t family,uint32_t nesting=0,DepthExecution* inherited=nullptr) {
    if(nesting>16) return;
    auto found=d.depth.commands.find(command); if(found==d.depth.commands.end()) return;
    DepthExecution local{}; DepthExecution& execution=inherited?*inherited:local;
    for(const DepthEvent& event:found->second.events) {
        if(event.kind==DepthEvent::Execute) { applyDepthCommands(d,event.secondary,family,nesting+1,&execution); continue; }
        if(event.kind==DepthEvent::BeginPass) { execution.best={}; continue; }
        if(event.kind==DepthEvent::Viewport) { execution.current=event.viewport; if(betterDepthViewport(event.viewport,execution.best)) execution.best=event.viewport; continue; }
        auto image=d.depth.images.find(event.image); if(image==d.depth.images.end()) continue;
        DepthImage& value=image->second;
        if(event.kind==DepthEvent::Layout) { value.layout=event.layout; if(event.owner!=VK_QUEUE_FAMILY_IGNORED) value.owner=event.owner; }
        else {
            const VkViewport& viewport=execution.best.width?execution.best:(execution.current.width?execution.current:event.viewport);
            if(value.written<=d.depth.previousPresent||betterDepthViewport(viewport,value.viewport)) value.viewport=viewport;
            value.stored=event.stored; value.written=d.depth.submitted; value.order=++d.depth.order; value.owner=family;
        }
    }
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence fence) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*queueDevice(queue); VkResult result=d.QueueSubmit(queue,count,submits,fence);
    if(result==VK_SUCCESS) { ++d.depth.submitted; uint32_t family=queues.count(queue)?queues[queue].family:VK_QUEUE_FAMILY_IGNORED;
        for(uint32_t i=0;i<count;++i) for(uint32_t j=0;j<submits[i].commandBufferCount;++j) applyDepthCommands(d,submits[i].pCommandBuffers[j],family);
    }
    return result;
}
#define SUBMIT2(name) \
EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk##name(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,VkFence fence) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*queueDevice(queue); VkResult result=d.name(queue,count,submits,fence); \
    if(result==VK_SUCCESS) { ++d.depth.submitted; uint32_t family=queues.count(queue)?queues[queue].family:VK_QUEUE_FAMILY_IGNORED; \
        for(uint32_t i=0;i<count;++i) for(uint32_t j=0;j<submits[i].commandBufferInfoCount;++j) applyDepthCommands(d,submits[i].pCommandBufferInfos[j].commandBuffer,family); } return result; }
SUBMIT2(QueueSubmit2)
SUBMIT2(QueueSubmit2KHR)
#undef SUBMIT2

static void beginDynamicDepth(Device& d,VkCommandBuffer command,const VkRenderingInfo* info) {
    auto& state=d.depth.commands[command]; state.dynamicImages.clear();
    DepthEvent begin{}; begin.kind=DepthEvent::BeginPass; state.events.push_back(begin);
    if(info->pDepthAttachment&&info->pDepthAttachment->imageView) {
        auto view=d.depth.views.find(info->pDepthAttachment->imageView);
        if(view!=d.depth.views.end()) {
            DepthEvent event{}; event.kind=DepthEvent::Use; event.image=view->second.image; event.stored=info->pDepthAttachment->storeOp==VK_ATTACHMENT_STORE_OP_STORE;
            event.viewport={static_cast<float>(info->renderArea.offset.x),static_cast<float>(info->renderArea.offset.y),static_cast<float>(info->renderArea.extent.width),static_cast<float>(info->renderArea.extent.height),0,1};
            state.events.push_back(event); state.dynamicImages.push_back(event.image);
            DepthEvent layout{}; layout.image=event.image; layout.layout=info->pDepthAttachment->imageLayout; state.events.push_back(layout);
        }
    }
}
static void endDynamicDepth(Device& d,VkCommandBuffer command) {
    auto& state=d.depth.commands[command];
    for(VkImage image:state.dynamicImages) {
        for(auto i=state.events.rbegin();i!=state.events.rend();++i) if(i->kind==DepthEvent::Use&&i->image==image) {
            DepthEvent event=*i; if(state.viewport.width&&state.viewport.height) event.viewport=state.viewport; state.events.push_back(event); break;
        }
    }
    state.dynamicImages.clear();
}
#define BEGIN_DYNAMIC(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command,const VkRenderingInfo* info) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); beginDynamicDepth(d,command,info); d.name(command,info); }
BEGIN_DYNAMIC(CmdBeginRendering)
BEGIN_DYNAMIC(CmdBeginRenderingKHR)
#undef BEGIN_DYNAMIC
#define END_DYNAMIC(name) \
EXPORT VKAPI_ATTR void VKAPI_CALL vk##name(VkCommandBuffer command) { \
    std::lock_guard<std::recursive_mutex> lock(guard); Device& d=*commandDevice(command); endDynamicDepth(d,command); d.name(command); }
END_DYNAMIC(CmdEndRendering)
END_DYNAMIC(CmdEndRenderingKHR)
#undef END_DYNAMIC

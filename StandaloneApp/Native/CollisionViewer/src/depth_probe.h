#pragma once

static VkDeviceSize probeReadbackOffset(const Frame& frame) { return frame.vertexCapacity*sizeof(Vertex); }
static void finishDepthProbe(Swapchain& swap,Frame& frame) {
    if(!frame.probePending) return;
    std::ofstream pixels(folder/L"visibility-depth.bin",std::ios::binary);
    VkDeviceSize offset=probeReadbackOffset(frame)+(smokeTest()?VkDeviceSize(swap.extent.width)*swap.extent.height*4:0);
    pixels.write(static_cast<const char*>(frame.mapped)+offset,size_t(swap.probeExtent.width)*swap.probeExtent.height*4); pixels.close();
    std::ofstream info(folder/L"visibility-depth.json");
    info<<"{\"request\":"<<frame.probeRequest<<",\"image\":\""<<std::hex<<frame.probeSource<<std::dec<<"\",\"format\":"<<frame.probeFormat<<",\"width\":"<<swap.probeExtent.width<<",\"height\":"<<swap.probeExtent.height<<",\"sourceWidth\":"<<frame.probeSourceExtent.width<<",\"sourceHeight\":"<<frame.probeSourceExtent.height<<",\"depthRange\":["<<frame.probeViewport.minDepth<<","<<frame.probeViewport.maxDepth<<"],\"matrix\":[";
    info.precision(9); for(int i=0;i<16;++i) { if(i) info<<","; info<<frame.probeCamera.matrix[i]; }
    info<<"],\"position\":["<<frame.probeCamera.position[0]<<","<<frame.probeCamera.position[1]<<","<<frame.probeCamera.position[2]<<"]}";
    frame.probePending=false; note("Visibility depth probe saved without changing the displayed frame.");
}
static void createProbeResources(Swapchain& swap,Frame& frame) {
    Device& d=*swap.device;
    if(!swap.probePass) {
        VkAttachmentDescription attachment{}; attachment.format=VK_FORMAT_R8G8B8A8_UNORM; attachment.samples=VK_SAMPLE_COUNT_1_BIT; attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&reference;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass=VK_SUBPASS_EXTERNAL; deps[0].dstSubpass=0; deps[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT; deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0; deps[1].dstSubpass=VK_SUBPASS_EXTERNAL; deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT; deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; pass.attachmentCount=1; pass.pAttachments=&attachment; pass.subpassCount=1; pass.pSubpasses=&subpass; pass.dependencyCount=2; pass.pDependencies=deps;
        require(d.CreateRenderPass(d.handle,&pass,nullptr,&swap.probePass),"Create visibility probe pass failed");
        VkShaderModule vertex{},fragment{}; VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader.codeSize=sizeof(kVertexShader); shader.pCode=kVertexShader;
        require(d.CreateShaderModule(d.handle,&shader,nullptr,&vertex),"Create probe vertex shader failed"); shader.codeSize=sizeof(kFragmentShader); shader.pCode=kFragmentShader;
        VkResult result=d.CreateShaderModule(d.handle,&shader,nullptr,&fragment);
        if(result!=VK_SUCCESS) { d.DestroyShaderModule(d.handle,vertex,nullptr); require(result,"Create probe fragment shader failed"); }
        try { Swapchain target{}; target.device=&d; target.layout=swap.layout; target.pass=swap.probePass; swap.probePipeline=createPipeline(target,vertex,fragment,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,false); }
        catch(...) { d.DestroyShaderModule(d.handle,vertex,nullptr); d.DestroyShaderModule(d.handle,fragment,nullptr); throw; }
        d.DestroyShaderModule(d.handle,vertex,nullptr); d.DestroyShaderModule(d.handle,fragment,nullptr);
    }
    if(frame.probeImage) return;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image.imageType=VK_IMAGE_TYPE_2D; image.format=VK_FORMAT_R8G8B8A8_UNORM; image.extent={swap.probeExtent.width,swap.probeExtent.height,1}; image.mipLevels=image.arrayLayers=1; image.samples=VK_SAMPLE_COUNT_1_BIT; image.tiling=VK_IMAGE_TILING_OPTIMAL; image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    require(d.CreateImage(d.handle,&image,nullptr,&frame.probeImage),"Create visibility probe image failed");
    VkMemoryRequirements requirements{}; d.GetImageMemoryRequirements(d.handle,frame.probeImage,&requirements);
    uint32_t type=~0u; for(uint32_t i=0;i<d.memory.memoryTypeCount;++i) if(requirements.memoryTypeBits&(1u<<i)) { type=i; if(d.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break; }
    if(type==~0u) throw std::runtime_error("No probe memory type");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
    require(d.AllocateMemory(d.handle,&allocation,nullptr,&frame.probeMemory),"Allocate probe memory failed"); require(d.BindImageMemory(d.handle,frame.probeImage,frame.probeMemory,0),"Bind probe memory failed");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=frame.probeImage; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=image.format; view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    require(d.CreateImageView(d.handle,&view,nullptr,&frame.probeView),"Create probe view failed");
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass=swap.probePass; framebuffer.attachmentCount=1; framebuffer.pAttachments=&frame.probeView; framebuffer.width=swap.probeExtent.width; framebuffer.height=swap.probeExtent.height; framebuffer.layers=1;
    require(d.CreateFramebuffer(d.handle,&framebuffer,nullptr,&frame.probeFramebuffer),"Create probe framebuffer failed");
}
template<class Push> static void recordDepthProbe(Swapchain& swap,Frame& frame,const DepthImage& depth,const GameCamera& camera,Push push) {
    Device& d=*swap.device; createProbeResources(swap,frame);
    VkDeviceSize extra=VkDeviceSize(swap.probeExtent.width)*swap.probeExtent.height*4+(smokeTest()?VkDeviceSize(swap.extent.width)*swap.extent.height*4:0);
    VkDeviceSize quadOffset=probeReadbackOffset(frame)+extra;
    Vertex quad[6]{}; const Vec3 points[6]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,-1,0},{1,1,0},{-1,1,0}};
    for(int i=0;i<6;++i) quad[i].position=points[i];
    memcpy(static_cast<char*>(frame.mapped)+quadOffset,quad,sizeof(quad));
    push.debug[0]=1;
    push.uv[0]=fabsf(depth.viewport.width)/depth.extent.width/swap.probeExtent.width; push.uv[1]=fabsf(depth.viewport.height)/depth.extent.height/swap.probeExtent.height;
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; pass.renderPass=swap.probePass; pass.framebuffer=frame.probeFramebuffer; pass.renderArea.extent=swap.probeExtent;
    d.CmdBeginRenderPass(frame.commands,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,static_cast<float>(swap.probeExtent.width),static_cast<float>(swap.probeExtent.height),0,1}; VkRect2D scissor{{0,0},swap.probeExtent};
    d.CmdSetViewport(frame.commands,0,1,&viewport); d.CmdSetScissor(frame.commands,0,1,&scissor); d.CmdBindPipeline(frame.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,swap.probePipeline);
    d.CmdBindVertexBuffers(frame.commands,0,1,&frame.vertices,&quadOffset); d.CmdBindDescriptorSets(frame.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,swap.layout,0,1,&frame.depthSet,0,nullptr);
    d.CmdPushConstants(frame.commands,swap.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push); d.CmdDraw(frame.commands,6,1,0,0); d.CmdEndRenderPass(frame.commands);
    VkBufferImageCopy copy{}; copy.bufferOffset=probeReadbackOffset(frame)+(smokeTest()?VkDeviceSize(swap.extent.width)*swap.extent.height*4:0); copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={swap.probeExtent.width,swap.probeExtent.height,1};
    d.CmdCopyImageToBuffer(frame.commands,frame.probeImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,frame.vertices,1,&copy);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    d.CmdPipelineBarrier(frame.commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    frame.probeRequest=visibilitySettings.probe; frame.probeSource=reinterpret_cast<uint64_t>(depth.handle); frame.probeCamera=camera; frame.probeViewport=depth.viewport; frame.probeSourceExtent=depth.extent; frame.probeFormat=depth.format; frame.probePending=true; swap.lastProbe=visibilitySettings.probe;
}

#pragma once

struct VisibilitySettings { uint64_t image=0; uint32_t mode=0,probe=0; float bias=.05f; };
static float fillDepthBias(float lineBias) { return std::max(lineBias,.10f); }
static VisibilitySettings visibilitySettings;
static void updateVisibilitySettings(Device& d) {
    static const bool initialized=[] { visibilitySettings.mode=smokeTest()?0u:2u; return true; }();
    (void)initialized;
    static ULONGLONG checked=0;
    if(GetTickCount64()-checked<500) return;
    checked=GetTickCount64();
    std::ifstream input(folder/L"Visibility.request"); VisibilitySettings value{};
    if(input>>std::hex>>value.image>>std::dec>>value.mode>>value.probe>>value.bias) {
        if(value.mode<=2&&std::isfinite(value.bias)&&value.bias>=0&&value.bias<=.5f) visibilitySettings=value;
    }
    static uint32_t dumped=0;
    if(visibilitySettings.probe!=dumped) {
        std::ofstream report(folder/L"visibility-candidates.json"); report<<"["; bool first=true;
        for(const auto& pair:d.depth.images) {
            const auto& i=pair.second;
            if(!first) report<<","; first=false;
            report<<"{\"image\":\""<<std::hex<<reinterpret_cast<uint64_t>(i.handle)<<std::dec<<"\",\"format\":"<<i.format<<",\"width\":"<<i.extent.width<<",\"height\":"<<i.extent.height<<",\"layout\":"<<i.layout<<",\"stored\":"<<i.stored<<",\"written\":"<<i.written<<",\"previousPresent\":"<<d.depth.previousPresent<<",\"owner\":"<<i.owner<<",\"viewport\":["<<i.viewport.x<<","<<i.viewport.y<<","<<i.viewport.width<<","<<i.viewport.height<<","<<i.viewport.minDepth<<","<<i.viewport.maxDepth<<"]}";
        }
        report<<"]"; dumped=visibilitySettings.probe;
    }
}

static DepthImage* chooseDepth(Swapchain& swap) {
    Device& d=*swap.device;
    updateVisibilitySettings(d);
    DepthImage* best=nullptr; double bestScore=-1e30;
    for(auto& pair:d.depth.images) {
        DepthImage& image=pair.second;
        if(visibilitySettings.image&&reinterpret_cast<uint64_t>(image.handle)!=visibilitySettings.image) continue;
        if(image.samples!=VK_SAMPLE_COUNT_1_BIT||!(image.usage&VK_IMAGE_USAGE_SAMPLED_BIT)||image.layers!=1||!image.stored||image.written<=d.depth.previousPresent) continue;
        if(image.layout==VK_IMAGE_LAYOUT_UNDEFINED||image.layout==VK_IMAGE_LAYOUT_PREINITIALIZED) continue;
        if(image.owner!=VK_QUEUE_FAMILY_IGNORED&&image.owner!=swap.family) continue;
        double width=fabs(image.viewport.width),height=fabs(image.viewport.height);
        if(width<swap.extent.width*.35||height<swap.extent.height*.35||width>swap.extent.width*1.5||height>swap.extent.height*1.5) continue;
        double aspectError=fabs(width/height-double(swap.extent.width)/swap.extent.height);
        if(aspectError>.04||image.viewport.minDepth==image.viewport.maxDepth) continue;
        double score=-fabs(log(width*height/(double(swap.extent.width)*swap.extent.height)))*100.0;
        if(!best||score>bestScore+.001||(fabs(score-bestScore)<.001&&image.order>best->order)) { best=&image; bestScore=score; }
    }
    static VkImage last{}; static ULONGLONG reportTime=0;
    if(last!=(best?best->handle:VK_NULL_HANDLE)||GetTickCount64()-reportTime>10000) {
        char message[380];
        if(best) sprintf_s(message,"Visibility depth=%p format=%d image=%ux%u viewport=(%.0f,%.0f,%.0f,%.0f) range=(%.3f,%.3f) mode=%u layout=%d stored=%d submit=%llu",reinterpret_cast<void*>(best->handle),best->format,best->extent.width,best->extent.height,best->viewport.x,best->viewport.y,best->viewport.width,best->viewport.height,best->viewport.minDepth,best->viewport.maxDepth,visibilitySettings.mode,best->layout,best->stored,best->written);
        else sprintf_s(message,"Visibility waiting for a fresh sampled screen depth buffer (%zu depth images, submit=%llu).",d.depth.images.size(),d.depth.submitted);
        note(message); last=best?best->handle:VK_NULL_HANDLE; reportTime=GetTickCount64();
    }
    if(best&&!best->sampleView) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; info.image=best->handle; info.viewType=VK_IMAGE_VIEW_TYPE_2D; info.format=best->format; info.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        if(d.CreateImageView(d.handle,&info,nullptr,&best->sampleView)!=VK_SUCCESS) { note("Cannot create a sampled view of screen depth; overlay withheld."); return nullptr; }
    }
    return best;
}

static void depthSamplingBarrier(Device& d,VkCommandBuffer commands,const DepthImage& image,bool restore) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=image.handle;
    barrier.subresourceRange={depthAspects(image.format),0,1,0,1}; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout=restore?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:image.layout;
    barrier.newLayout=restore?image.layout:VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=restore?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=restore?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
    d.CmdPipelineBarrier(commands,restore?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,restore?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}

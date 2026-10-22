#include "../src/vulkan_layer.cpp"

static void check(bool value,const char* label) { if(!value) throw std::runtime_error(label); }
int main() {
    try {
        check(visibilitySettings.bias==.05f,"Default depth tolerance did not include stability margin");
        check(fillDepthBias(visibilitySettings.bias)==.10f&&fillDepthBias(.2f)==.2f,"Filled surfaces did not receive independent depth tolerance");
        folder=std::filesystem::current_path();
        Device device{};
        VkImage image=reinterpret_cast<VkImage>(uintptr_t(1));
        VkCommandBuffer primary=reinterpret_cast<VkCommandBuffer>(uintptr_t(2)),secondary=reinterpret_cast<VkCommandBuffer>(uintptr_t(3));
        DepthImage value{}; value.handle=image; value.format=VK_FORMAT_D32_SFLOAT; value.extent={128,128,1}; value.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; value.samples=VK_SAMPLE_COUNT_1_BIT; value.layers=value.mips=1;
        value.sampleView=reinterpret_cast<VkImageView>(uintptr_t(4)); device.depth.images[image]=value;
        DepthEvent viewport{}; viewport.kind=DepthEvent::Viewport; viewport.viewport={0,128,128,-128,1,0};
        device.depth.commands[secondary].events.push_back(viewport);
        DepthEvent weapon=viewport; weapon.viewport.maxDepth=.9f; device.depth.commands[secondary].events.push_back(weapon);
        DepthEvent execute{}; execute.kind=DepthEvent::Execute; execute.secondary=secondary;
        DepthEvent use{}; use.kind=DepthEvent::Use; use.image=image; use.stored=true; use.viewport={0,0,128,128,0,1};
        DepthEvent layout{}; layout.image=image; layout.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        device.depth.commands[primary].events={execute,use,layout};
        device.depth.submitted=1; applyDepthCommands(device,primary,7);
        const DepthImage& tracked=device.depth.images.at(image);
        check(tracked.viewport.height==-128&&tracked.viewport.minDepth==1&&tracked.viewport.maxDepth==0,"Secondary viewport and reversed depth lost");
        check(tracked.layout==layout.layout&&tracked.stored&&tracked.written==1&&tracked.owner==7,"Submitted depth state incorrect");
        Swapchain swap{}; swap.device=&device; swap.extent={128,128}; swap.family=7;
        check(chooseDepth(swap)==&tracked,"Fresh depth was not selected");
        device.depth.previousPresent=1; check(chooseDepth(swap)==nullptr,"Stale depth must withhold overlay");
        device.depth.submitted=2; applyDepthCommands(device,primary,7); check(chooseDepth(swap)==&tracked,"Reusable command did not refresh depth");
        swap.family=9; check(chooseDepth(swap)==nullptr,"Wrong queue family depth selected");
        swap.family=7; device.depth.images[image].stored=false; check(chooseDepth(swap)==nullptr,"Discarded depth selected");
        device.depth.images[image].stored=true; device.depth.images[image].samples=VK_SAMPLE_COUNT_4_BIT; check(chooseDepth(swap)==nullptr,"Multisampled depth selected as sampler2D");
        device.depth.images.erase(image); applyDepthCommands(device,primary,7); check(device.depth.images.empty(),"Destroyed image resurrected by command history");
        puts("PASS: submitted layouts, secondary viewports, reversed depth, freshness, queue ownership, discarded and multisampled buffers.");
        return 0;
    } catch(const std::exception& error) { fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}

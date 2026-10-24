#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <algorithm>

static void check(VkResult result,const char* operation) { if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR) { fprintf(stderr,"%s: %d\n",operation,result); throw std::runtime_error(operation); } }
int main() {
    try {
        HMODULE library=LoadLibraryW(L"vulkan-1.dll");
        auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library,"vkGetInstanceProcAddr"));
        auto createInstance=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
        const char* extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="DOOM collision layer smoke test"; app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo=&app; instanceInfo.enabledExtensionCount=2; instanceInfo.ppEnabledExtensionNames=extensions;
        VkInstance instance{}; check(createInstance(&instanceInfo,nullptr,&instance),"CreateInstance");
#define INSTANCE(name) auto name=reinterpret_cast<PFN_vk##name>(gipa(instance,"vk" #name));
        INSTANCE(EnumeratePhysicalDevices) INSTANCE(GetPhysicalDeviceQueueFamilyProperties) INSTANCE(GetPhysicalDeviceSurfaceSupportKHR)
        INSTANCE(CreateWin32SurfaceKHR) INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR) INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR) INSTANCE(CreateDevice) INSTANCE(DestroySurfaceKHR) INSTANCE(DestroyInstance)
        INSTANCE(GetPhysicalDeviceMemoryProperties)
        auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));
        WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"CollisionLayerHiddenSmokeWindow";
        RegisterClassW(&wc); HWND window=CreateWindowExW(0,wc.lpszClassName,L"Collision layer smoke test",WS_POPUP,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) throw std::runtime_error("Create hidden test window failed");
        VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR}; surfaceInfo.hinstance=wc.hInstance; surfaceInfo.hwnd=window;
        VkSurfaceKHR surface{}; check(CreateWin32SurfaceKHR(instance,&surfaceInfo,nullptr,&surface),"CreateSurface");
        uint32_t count=0; check(EnumeratePhysicalDevices(instance,&count,nullptr),"EnumeratePhysicalDevices"); std::vector<VkPhysicalDevice> physicals(count); check(EnumeratePhysicalDevices(instance,&count,physicals.data()),"EnumeratePhysicalDevices");
        VkPhysicalDevice physical{}; uint32_t family=~0u;
        for(auto candidate:physicals) {
            uint32_t n=0; GetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr); std::vector<VkQueueFamilyProperties> properties(n); GetPhysicalDeviceQueueFamilyProperties(candidate,&n,properties.data());
            for(uint32_t i=0;i<n;++i) { VkBool32 supported=VK_FALSE; GetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&supported); if((properties[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&supported) { physical=candidate; family=i; break; } }
            if(physical) break;
        }
        if(!physical) throw std::runtime_error("No graphics/present queue");
        wchar_t splitMode[32]{}; GetEnvironmentVariableW(L"DCV_TEST_SPLIT_PRESENT",splitMode,32);
        bool split=splitMode[0]!=0,exclusive=split&&splitMode[0]=='e'; uint32_t presentFamily=family;
        if(split) {
            uint32_t n=0; GetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr); std::vector<VkQueueFamilyProperties> properties(n); GetPhysicalDeviceQueueFamilyProperties(physical,&n,properties.data());
            for(uint32_t i=0;i<n;++i) {
                VkBool32 supported=VK_FALSE; GetPhysicalDeviceSurfaceSupportKHR(physical,i,surface,&supported);
                if(i!=family&&!(properties[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&supported) { presentFamily=i; break; }
            }
            if(presentFamily==family) throw std::runtime_error("No non-graphics presentation queue for split-queue GPU test");
        }
        float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex=family; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
        VkDeviceQueueCreateInfo queueInfos[2]{queueInfo,queueInfo}; queueInfos[1].queueFamilyIndex=presentFamily;
        const char* deviceExtension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=split?2u:1u; deviceInfo.pQueueCreateInfos=queueInfos; deviceInfo.enabledExtensionCount=1; deviceInfo.ppEnabledExtensionNames=&deviceExtension;
        VkDevice device{}; check(CreateDevice(physical,&deviceInfo,nullptr,&device),"CreateDevice");
#define DEVICE(name) auto name=reinterpret_cast<PFN_vk##name>(gdpa(device,"vk" #name));
        DEVICE(GetDeviceQueue) DEVICE(CreateSwapchainKHR) DEVICE(GetSwapchainImagesKHR) DEVICE(AcquireNextImageKHR) DEVICE(QueuePresentKHR) DEVICE(DestroySwapchainKHR)
        DEVICE(CreateCommandPool) DEVICE(AllocateCommandBuffers) DEVICE(BeginCommandBuffer) DEVICE(EndCommandBuffer) DEVICE(ResetCommandBuffer) DEVICE(DestroyCommandPool)
        DEVICE(CmdPipelineBarrier) DEVICE(CmdClearColorImage) DEVICE(CreateSemaphore) DEVICE(DestroySemaphore) DEVICE(QueueSubmit) DEVICE(DeviceWaitIdle) DEVICE(DestroyDevice)
        DEVICE(CreateImage) DEVICE(DestroyImage) DEVICE(GetImageMemoryRequirements) DEVICE(AllocateMemory) DEVICE(FreeMemory) DEVICE(BindImageMemory) DEVICE(CreateImageView) DEVICE(DestroyImageView)
        DEVICE(CreateRenderPass) DEVICE(DestroyRenderPass) DEVICE(CreateFramebuffer) DEVICE(DestroyFramebuffer) DEVICE(CmdBeginRenderPass) DEVICE(CmdEndRenderPass) DEVICE(CmdClearAttachments)
        DEVICE(CmdSetViewport)
        VkQueue queue{}; GetDeviceQueue(device,family,0,&queue);
        VkQueue presentQueue=queue; if(split) GetDeviceQueue(device,presentFamily,0,&presentQueue);
        VkSurfaceCapabilitiesKHR caps{}; check(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"SurfaceCapabilities");
        check(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"SurfaceFormats"); std::vector<VkSurfaceFormatKHR> formats(count); check(GetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"SurfaceFormats");
        VkSurfaceFormatKHR format=formats[0]; for(auto f:formats) if(f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM) { format=f; break; }
        VkExtent2D extent=caps.currentExtent; if(extent.width==0xffffffffu) extent={128,128}; if(!extent.width||!extent.height) throw std::runtime_error("Zero surface extent");
        VkSwapchainCreateInfoKHR swapInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; swapInfo.surface=surface; swapInfo.minImageCount=caps.minImageCount; swapInfo.imageFormat=format.format; swapInfo.imageColorSpace=format.colorSpace;
        swapInfo.imageExtent=extent; swapInfo.imageArrayLayers=1; swapInfo.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT; swapInfo.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
        uint32_t sharedFamilies[]{family,presentFamily};
        if(split&&!exclusive) { swapInfo.imageSharingMode=VK_SHARING_MODE_CONCURRENT; swapInfo.queueFamilyIndexCount=2; swapInfo.pQueueFamilyIndices=sharedFamilies; }
        swapInfo.preTransform=caps.currentTransform; swapInfo.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; swapInfo.presentMode=VK_PRESENT_MODE_FIFO_KHR; swapInfo.clipped=VK_TRUE;
        VkSwapchainKHR swap{}; check(CreateSwapchainKHR(device,&swapInfo,nullptr,&swap),"CreateSwapchain");
        VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; depthInfo.imageType=VK_IMAGE_TYPE_2D; depthInfo.format=VK_FORMAT_D32_SFLOAT; depthInfo.extent={extent.width,extent.height,1};
        depthInfo.mipLevels=depthInfo.arrayLayers=1; depthInfo.samples=VK_SAMPLE_COUNT_1_BIT; depthInfo.tiling=VK_IMAGE_TILING_OPTIMAL; depthInfo.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImage depthImage{}; check(CreateImage(device,&depthInfo,nullptr,&depthImage),"Create depth image");
        VkMemoryRequirements depthRequirements{}; GetImageMemoryRequirements(device,depthImage,&depthRequirements);
        VkPhysicalDeviceMemoryProperties memoryProperties{}; GetPhysicalDeviceMemoryProperties(physical,&memoryProperties);
        uint32_t depthType=~0u; for(uint32_t i=0;i<memoryProperties.memoryTypeCount;++i) if(depthRequirements.memoryTypeBits&(1u<<i)) { depthType=i; if(memoryProperties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break; }
        VkMemoryAllocateInfo depthAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; depthAllocation.allocationSize=depthRequirements.size; depthAllocation.memoryTypeIndex=depthType;
        VkDeviceMemory depthMemory{}; check(AllocateMemory(device,&depthAllocation,nullptr,&depthMemory),"Allocate depth memory"); check(BindImageMemory(device,depthImage,depthMemory,0),"Bind depth memory");
        VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; depthViewInfo.image=depthImage; depthViewInfo.viewType=VK_IMAGE_VIEW_TYPE_2D; depthViewInfo.format=depthInfo.format; depthViewInfo.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        VkImageView depthView{}; check(CreateImageView(device,&depthViewInfo,nullptr,&depthView),"Create depth view");
        VkAttachmentDescription depthAttachment{}; depthAttachment.format=depthInfo.format; depthAttachment.samples=VK_SAMPLE_COUNT_1_BIT; depthAttachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; depthAttachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; depthAttachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; depthAttachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; depthAttachment.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference depthReference{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}; VkSubpassDescription depthSubpass{}; depthSubpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; depthSubpass.pDepthStencilAttachment=&depthReference;
        VkRenderPassCreateInfo depthPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; depthPassInfo.attachmentCount=1; depthPassInfo.pAttachments=&depthAttachment; depthPassInfo.subpassCount=1; depthPassInfo.pSubpasses=&depthSubpass;
        VkRenderPass depthPass{}; check(CreateRenderPass(device,&depthPassInfo,nullptr,&depthPass),"Create depth pass");
        VkFramebufferCreateInfo depthFramebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; depthFramebufferInfo.renderPass=depthPass; depthFramebufferInfo.attachmentCount=1; depthFramebufferInfo.pAttachments=&depthView; depthFramebufferInfo.width=extent.width; depthFramebufferInfo.height=extent.height; depthFramebufferInfo.layers=1;
        VkFramebuffer depthFramebuffer{}; check(CreateFramebuffer(device,&depthFramebufferInfo,nullptr,&depthFramebuffer),"Create depth framebuffer");
        check(GetSwapchainImagesKHR(device,swap,&count,nullptr),"SwapchainImages"); std::vector<VkImage> images(count); check(GetSwapchainImagesKHR(device,swap,&count,images.data()),"SwapchainImages");
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; poolInfo.queueFamilyIndex=family;
        VkCommandPool pool{}; check(CreateCommandPool(device,&poolInfo,nullptr,&pool),"CreateCommandPool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool=pool; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount=1;
        VkCommandBuffer commands{}; check(AllocateCommandBuffers(device,&allocate,&commands),"AllocateCommandBuffers");
        VkCommandPool presentPool{}; VkCommandBuffer presentCommands{};
        if(exclusive) {
            poolInfo.queueFamilyIndex=presentFamily; check(CreateCommandPool(device,&poolInfo,nullptr,&presentPool),"Create app presentation pool");
            allocate.commandPool=presentPool; check(AllocateCommandBuffers(device,&allocate,&presentCommands),"Allocate app presentation commands");
        }
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore acquired{},ready{}; check(CreateSemaphore(device,&semInfo,nullptr,&acquired),"CreateSemaphore"); check(CreateSemaphore(device,&semInfo,nullptr,&ready),"CreateSemaphore");
        VkSemaphore graphicsComplete{}; if(exclusive) check(CreateSemaphore(device,&semInfo,nullptr,&graphicsComplete),"Create app graphics semaphore");
        for(int frame=0;frame<(GetEnvironmentVariableW(L"DCV_PROBE_TEST",nullptr,0)?8:3);++frame) {
            uint32_t index=0; check(AcquireNextImageKHR(device,swap,2000000000ull,acquired,VK_NULL_HANDLE,&index),"AcquireNextImage");
            check(ResetCommandBuffer(commands,0),"ResetCommandBuffer"); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(BeginCommandBuffer(commands,&begin),"BeginCommandBuffer");
            wchar_t visibilityCase[32]{}; GetEnvironmentVariableW(L"DCV_OCCLUSION_TEST",visibilityCase,32);
            VkClearValue depthClear{}; depthClear.depthStencil={visibilityCase[0]=='i'?0.0f:1.0f,0};
            if(visibilityCase[0]=='r') { VkViewport reversed{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),1,0}; CmdSetViewport(commands,0,1,&reversed); }
            VkRenderPassBeginInfo depthBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; depthBegin.renderPass=depthPass; depthBegin.framebuffer=depthFramebuffer; depthBegin.renderArea.extent=extent; depthBegin.clearValueCount=1; depthBegin.pClearValues=&depthClear;
            CmdBeginRenderPass(commands,&depthBegin,VK_SUBPASS_CONTENTS_INLINE);
            if(visibilityCase[0]) {
                VkClearAttachment wall{}; wall.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT; wall.clearValue.depthStencil={(visibilityCase[0]=='r'||visibilityCase[0]=='i')?.05f:.95f,0};
                VkClearRect rect{}; rect.layerCount=1; rect.rect.extent={(visibilityCase[0]=='p'||(visibilityCase[0]=='i'&&visibilityCase[9]=='p'))?extent.width/2:extent.width,extent.height}; CmdClearAttachments(commands,1,&wall,1,&rect);
            }
            CmdEndRenderPass(commands);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.image=images[index]; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            CmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkClearColorValue black{{0,0,0,1}}; CmdClearColorImage(commands,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&barrier.subresourceRange);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=0; barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            if(exclusive) { barrier.srcQueueFamilyIndex=family; barrier.dstQueueFamilyIndex=presentFamily; }
            CmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier); check(EndCommandBuffer(commands),"EndCommandBuffer");
            if(exclusive) {
                check(ResetCommandBuffer(presentCommands,0),"Reset app presentation command"); check(BeginCommandBuffer(presentCommands,&begin),"Begin app presentation command");
                barrier.srcAccessMask=barrier.dstAccessMask=0;
                CmdPipelineBarrier(presentCommands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
                check(EndCommandBuffer(presentCommands),"End app presentation command");
            }
            VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount=1; submit.pWaitSemaphores=&acquired; submit.pWaitDstStageMask=&stage; submit.commandBufferCount=1; submit.pCommandBuffers=&commands; submit.signalSemaphoreCount=1; submit.pSignalSemaphores=&ready;
            if(exclusive) submit.pSignalSemaphores=&graphicsComplete;
            check(QueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"QueueSubmit");
            if(exclusive) {
                VkPipelineStageFlags wait=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                VkSubmitInfo post{VK_STRUCTURE_TYPE_SUBMIT_INFO}; post.waitSemaphoreCount=1; post.pWaitSemaphores=&graphicsComplete; post.pWaitDstStageMask=&wait;
                post.commandBufferCount=1; post.pCommandBuffers=&presentCommands; post.signalSemaphoreCount=1; post.pSignalSemaphores=&ready;
                check(QueueSubmit(presentQueue,1,&post,VK_NULL_HANDLE),"App presentation acquire submit");
            }
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount=1; present.pWaitSemaphores=&ready; present.swapchainCount=1; present.pSwapchains=&swap; present.pImageIndices=&index;
            check(QueuePresentKHR(presentQueue,&present),"QueuePresent"); check(DeviceWaitIdle(device),"DeviceWaitIdle");
        }
        DestroySemaphore(device,acquired,nullptr); DestroySemaphore(device,ready,nullptr); DestroyCommandPool(device,pool,nullptr); DestroySwapchainKHR(device,swap,nullptr);
        if(graphicsComplete) DestroySemaphore(device,graphicsComplete,nullptr); if(presentPool) DestroyCommandPool(device,presentPool,nullptr);
        DestroyFramebuffer(device,depthFramebuffer,nullptr); DestroyRenderPass(device,depthPass,nullptr); DestroyImageView(device,depthView,nullptr); DestroyImage(device,depthImage,nullptr); FreeMemory(device,depthMemory,nullptr);
        DestroyDevice(device,nullptr); DestroySurfaceKHR(instance,surface,nullptr); DestroyInstance(instance,nullptr); DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
        puts("Vulkan smoke test completed; inspect smoke-result.json for GPU pixel validation."); return 0;
    } catch(const std::exception& error) { fprintf(stderr,"Smoke test failed: %s\n",error.what()); return 1; }
}

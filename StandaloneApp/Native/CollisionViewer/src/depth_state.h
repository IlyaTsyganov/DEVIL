#pragma once

// Observe application-owned resources. We only sample a stored, single-sample,
// screen-sized depth attachment and restore its layout before presentation.
struct DepthImage {
    VkImage handle{}; VkFormat format{}; VkExtent3D extent{}; VkImageUsageFlags usage{};
    VkSampleCountFlagBits samples{}; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t layers{},mips{},owner=VK_QUEUE_FAMILY_IGNORED;
    VkImageView sampleView{}; VkViewport viewport{};
    uint64_t written{},order{}; bool stored=false;
};
struct DepthView { VkImage image{}; VkImageSubresourceRange range{}; };
struct DepthAttachment { VkImageLayout initial{},final{}; VkAttachmentLoadOp load{}; VkAttachmentStoreOp store{}; bool depth=false; };
struct DepthPass { std::vector<DepthAttachment> attachments; std::vector<uint32_t> subpasses; };
struct DepthFramebuffer { std::vector<VkImageView> views; VkExtent2D extent{}; };
struct DepthEvent {
    enum Kind { Layout,Use,Execute,Viewport,BeginPass } kind=Layout;
    VkImage image{}; VkImageLayout layout{}; VkViewport viewport{};
    uint32_t owner=VK_QUEUE_FAMILY_IGNORED; bool stored=false;
    VkCommandBuffer secondary{};
};
struct DepthCommand {
    std::vector<DepthEvent> events;
    VkRenderPass pass{}; VkFramebuffer framebuffer{}; uint32_t subpass{};
    VkViewport viewport{}; std::vector<VkImageView> temporaryViews;
    std::vector<VkImage> dynamicImages;
};
struct DepthState {
    std::unordered_map<VkImage,DepthImage> images;
    std::unordered_map<VkImageView,DepthView> views;
    std::unordered_map<VkRenderPass,DepthPass> passes;
    std::unordered_map<VkFramebuffer,DepthFramebuffer> framebuffers;
    std::unordered_map<VkCommandBuffer,DepthCommand> commands;
    uint64_t submitted=0,previousPresent=0,order=0;
};
static bool depthFormat(VkFormat format) {
    return format==VK_FORMAT_D16_UNORM||format==VK_FORMAT_X8_D24_UNORM_PACK32||format==VK_FORMAT_D32_SFLOAT||format==VK_FORMAT_D16_UNORM_S8_UINT||format==VK_FORMAT_D24_UNORM_S8_UINT||format==VK_FORMAT_D32_SFLOAT_S8_UINT;
}
static VkImageAspectFlags depthAspects(VkFormat format) {
    return VK_IMAGE_ASPECT_DEPTH_BIT|((format==VK_FORMAT_D16_UNORM_S8_UINT||format==VK_FORMAT_D24_UNORM_S8_UINT||format==VK_FORMAT_D32_SFLOAT_S8_UINT)?VK_IMAGE_ASPECT_STENCIL_BIT:0);
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
extern "C" {
#include <vulkan/vk_layer.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <share.h>
#include "game_camera.h"
#include "shaders_spirv.h"
#include "depth_state.h"

#define EXPORT extern "C" __declspec(dllexport)
static constexpr char kLayerName[]="VK_LAYER_DOOM_collision";
static std::recursive_mutex guard;
static std::filesystem::path folder;
static FILE* logfile;
static void note(const char* message) {
    if(folder.empty()) {
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&note),&module);
        wchar_t path[32768]; GetModuleFileNameW(module,path,32768);
        folder=std::filesystem::path(path).parent_path();
        logfile=_wfsopen((folder/L"VulkanViewer.log").c_str(),L"w",_SH_DENYNO);
    }
    if(logfile) { fprintf(logfile,"%llu %s\n",GetTickCount64(),message); fflush(logfile); }
}
static void require(VkResult result,const char* operation) { if(result!=VK_SUCCESS) { note(operation); throw std::runtime_error(operation); } }
template<class T> static void* key(T object) { return *reinterpret_cast<void**>(object); }

struct Vec3 { float x,y,z; };
struct Vertex { Vec3 position; float color[4]; };
struct Geometry { uint32_t kind; std::vector<Vec3> lines,triangles; };
struct Mesh {
    uint32_t kind; float bounds[6]; std::shared_ptr<const Geometry> geometry;
    float matrix[12]{1,0,0,0,0,1,0,0,0,0,1,0};
    Vec3 world(Vec3 p) const {
        return {matrix[0]*p.x+matrix[1]*p.y+matrix[2]*p.z+matrix[3],
                matrix[4]*p.x+matrix[5]*p.y+matrix[6]*p.z+matrix[7],
                matrix[8]*p.x+matrix[9]*p.y+matrix[10]*p.z+matrix[11]};
    }
};
static std::vector<Mesh> meshes;
struct SpatialNode { float bounds[6]; uint32_t begin{},count{},left=~0u,right=~0u; };
static std::vector<SpatialNode> spatialNodes;
static std::vector<uint32_t> spatialOrder;
static uint32_t buildSpatialNodeFor(const std::vector<Mesh>& source,std::vector<SpatialNode>& nodes,std::vector<uint32_t>& order,uint32_t begin,uint32_t end) {
    SpatialNode node{}; node.begin=begin; node.count=end-begin;
    for(int axis=0;axis<3;++axis) { node.bounds[axis]=FLT_MAX; node.bounds[axis+3]=-FLT_MAX; }
    for(uint32_t i=begin;i<end;++i) for(int axis=0;axis<3;++axis) {
        const float* box=source[order[i]].bounds;
        node.bounds[axis]=std::min(node.bounds[axis],box[axis]); node.bounds[axis+3]=std::max(node.bounds[axis+3],box[axis+3]);
    }
    uint32_t index=static_cast<uint32_t>(nodes.size()); nodes.push_back(node);
    if(node.count>12) {
        int axis=0; for(int k=1;k<3;++k) if(node.bounds[k+3]-node.bounds[k]>node.bounds[axis+3]-node.bounds[axis]) axis=k;
        uint32_t middle=begin+node.count/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[axis,&source](uint32_t a,uint32_t b) {
            const float* x=source[a].bounds; const float* y=source[b].bounds;
            return x[axis]+x[axis+3]<y[axis]+y[axis+3];
        });
        uint32_t left=buildSpatialNodeFor(source,nodes,order,begin,middle),right=buildSpatialNodeFor(source,nodes,order,middle,end);
        nodes[index].left=left; nodes[index].right=right;
    }
    return index;
}
static uint32_t buildSpatialNode(uint32_t begin,uint32_t end) { return buildSpatialNodeFor(meshes,spatialNodes,spatialOrder,begin,end); }
static bool sceneLoaded=false, showTriggers=true, showCollision=true, showWalls=true, enabled=true;
static uint32_t collisionMode=1;
static char sceneMap[64]{};
static constexpr uint32_t kInitialVertexCapacity=250000;
static float radius=40.0f;
static float cullingRadius() { return radius+std::max(0.5f,radius*0.01f); }

static void loadScene() {
    if(sceneLoaded) return;
    sceneLoaded=true;
    std::ifstream input(folder/L"scene.dcv",std::ios::binary);
    auto read=[&](void* out,size_t size) { if(!input.read(static_cast<char*>(out),size)) throw std::runtime_error("Truncated scene.dcv"); };
    try {
        char magic[4]; uint32_t version=0,count=0;
        read(magic,4); read(&version,4); read(sceneMap,64); read(&count,4);
        bool instanced=memcmp(magic,"DCV2",4)==0&&version==2;
        if((!instanced&&(memcmp(magic,"DCV1",4)||version!=1))||count>100000||!memchr(sceneMap,0,64)) throw std::runtime_error("Invalid scene header");
        std::vector<std::shared_ptr<const Geometry>> geometries;
        for(uint32_t i=0;i<count;++i) {
            Mesh mesh{}; uint32_t edges=0,triangles=0;
            read(&mesh.kind,4); read(&edges,4); read(&triangles,4); read(mesh.bounds,24);
            if(mesh.kind>1 || edges>1000000 || triangles>1000000) throw std::runtime_error("Invalid scene mesh");
            auto geometry=std::make_shared<Geometry>(); geometry->kind=mesh.kind;
            geometry->lines.resize(size_t(edges)*2); geometry->triangles.resize(size_t(triangles)*3);
            read(geometry->lines.data(),geometry->lines.size()*sizeof(Vec3)); read(geometry->triangles.data(),geometry->triangles.size()*sizeof(Vec3));
            if(instanced) geometries.push_back(geometry);
            else { mesh.geometry=geometry; meshes.push_back(std::move(mesh)); }
        }
        if(instanced) {
            uint32_t instances=0; read(&instances,4);
            if(instances>1000000) throw std::runtime_error("Too many scene instances");
            meshes.reserve(instances);
            for(uint32_t i=0;i<instances;++i) {
                uint32_t index=0; Mesh mesh{};
                read(&index,4); read(mesh.matrix,48); read(mesh.bounds,24);
                if(index>=geometries.size()) throw std::runtime_error("Invalid geometry instance reference");
                for(float v:mesh.matrix) if(!std::isfinite(v)) throw std::runtime_error("Invalid instance transform");
                for(int axis=0;axis<3;++axis) if(!std::isfinite(mesh.bounds[axis])||!std::isfinite(mesh.bounds[axis+3])||mesh.bounds[axis]>mesh.bounds[axis+3]) throw std::runtime_error("Invalid instance bounds");
                mesh.geometry=geometries[index]; mesh.kind=mesh.geometry->kind;
                meshes.push_back(std::move(mesh));
            }
        }
        spatialNodes.clear(); spatialOrder.resize(meshes.size()); std::iota(spatialOrder.begin(),spatialOrder.end(),0u);
        if(!meshes.empty()) { spatialNodes.reserve(meshes.size()/4); buildSpatialNode(0,static_cast<uint32_t>(meshes.size())); }
        char message[160]; sprintf_s(message,"Scene loaded: %s, %zu instances, %zu shared geometries.",sceneMap,meshes.size(),instanced?geometries.size():meshes.size()); note(message);
    } catch(const std::exception& error) { meshes.clear(); spatialNodes.clear(); spatialOrder.clear(); note(error.what()); }
}

struct Instance {
    VkInstance handle{};
    PFN_vkGetInstanceProcAddr gipa{};
    PFN_GetPhysicalDeviceProcAddr physicalGipa{};
    PFN_vkGetPhysicalDeviceMemoryProperties memoryProperties{};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties queueProperties{};
};

#define DEVICE_FUNCTIONS(X) \
 X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(QueuePresentKHR) \
 X(CreateRenderPass) X(DestroyRenderPass) X(CreateImageView) X(DestroyImageView) X(CreateFramebuffer) X(DestroyFramebuffer) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) X(CreateGraphicsPipelines) X(DestroyPipeline) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
 X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(QueueSubmit) X(DeviceWaitIdle) \
 X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdSetViewport) X(CmdSetScissor) X(CmdBindVertexBuffers) X(CmdPushConstants) X(CmdDraw) X(CmdPipelineBarrier) X(CmdCopyImageToBuffer)

#define DEPTH_FUNCTIONS(X) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateSampler) X(DestroySampler) X(CmdBindDescriptorSets) \
 X(CmdNextSubpass) X(CmdExecuteCommands) X(FreeCommandBuffers) X(CmdBeginRendering) X(CmdEndRendering) X(CmdBeginRenderingKHR) X(CmdEndRenderingKHR) \
 X(CreateRenderPass2) X(CreateRenderPass2KHR) X(CmdBeginRenderPass2) X(CmdBeginRenderPass2KHR) X(CmdNextSubpass2) X(CmdNextSubpass2KHR) X(CmdEndRenderPass2) X(CmdEndRenderPass2KHR) \
 X(CmdPipelineBarrier2) X(CmdPipelineBarrier2KHR) X(QueueSubmit2) X(QueueSubmit2KHR)

struct Device {
    VkDevice handle{};
    VkPhysicalDevice physical{};
    Instance* instance{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkSetDeviceLoaderData setLoaderData{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<VkQueueFamilyProperties> families;
#define DECLARE(name) PFN_vk##name name{};
    DEVICE_FUNCTIONS(DECLARE)
    DEPTH_FUNCTIONS(DECLARE)
#undef DECLARE
    DepthState depth;
};
struct Queue { Device* device; uint32_t family; };
struct Frame {
    VkImage image{}; VkImageView view{}; VkFramebuffer framebuffer{};
    VkImage viewerDepth{}; VkDeviceMemory viewerDepthMemory{}; VkImageView viewerDepthView{};
    VkCommandBuffer commands{}; VkSemaphore complete{}; VkFence fence{};
    VkBuffer vertices{}; VkDeviceMemory allocation{}; void* mapped{}; VkDeviceSize vertexCapacity{};
    VkDescriptorSet depthSet{}; VkImage boundDepth{};
    VkImage probeImage{}; VkDeviceMemory probeMemory{}; VkImageView probeView{}; VkFramebuffer probeFramebuffer{};
    bool probePending=false; uint32_t probeRequest=0; uint64_t probeSource=0; GameCamera probeCamera{};
    VkViewport probeViewport{}; VkFormat probeFormat{}; VkExtent3D probeSourceExtent{};
    VkCommandPool handoffPool{}; VkCommandBuffer releaseToGraphics{},acquireForPresent{};
    VkSemaphore graphicsReady{},presentReady{}; uint32_t handoffFamily=~0u;
};
static bool overlayDepthTestEnabled(VkPrimitiveTopology topology) { return topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; }
static bool overlayDepthWriteEnabled(VkPrimitiveTopology topology) { return topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; }
struct Swapchain {
    Device* device{}; VkSwapchainKHR handle{}; VkExtent2D extent{}; VkFormat format{};
    VkFormat viewerDepthFormat=VK_FORMAT_UNDEFINED;
    VkRenderPass pass{}; VkPipelineLayout layout{}; VkPipeline lines{},fill{}; VkCommandPool pool{};
    std::vector<Frame> frames; bool ready=false; uint32_t family=~0u; uint64_t rendered=0;
    VkDescriptorSetLayout depthLayout{}; VkDescriptorPool depthPool{}; VkSampler depthSampler{};
    VkRenderPass probePass{}; VkPipeline probePipeline{}; VkExtent2D probeExtent{}; uint32_t lastProbe=0;
    VkSharingMode sharing=VK_SHARING_MODE_EXCLUSIVE; std::vector<uint32_t> sharingFamilies;
    VkResult submitFailure=VK_SUCCESS;
};
static bool smokeTest();
static bool sceneTest() { static const bool value=GetEnvironmentVariableW(L"DCV_SCENE_TEST",nullptr,0)>0; return value; }
static std::unordered_map<void*,std::unique_ptr<Instance>> instances;
static std::unordered_map<void*,std::unique_ptr<Device>> devices;
static std::unordered_map<VkQueue,Queue> queues;
static std::unordered_map<VkSwapchainKHR,std::unique_ptr<Swapchain>> swaps;
static Device* findDevice(VkDevice device) { auto i=devices.find(key(device)); return i==devices.end()?nullptr:i->second.get(); }

static void cleanup(Swapchain& swap) {
    Device& d=*swap.device;
    for(auto& frame:swap.frames) {
        if(frame.mapped) d.UnmapMemory(d.handle,frame.allocation);
        if(frame.vertices) d.DestroyBuffer(d.handle,frame.vertices,nullptr);
        if(frame.allocation) d.FreeMemory(d.handle,frame.allocation,nullptr);
        if(frame.fence) d.DestroyFence(d.handle,frame.fence,nullptr);
        if(frame.complete) d.DestroySemaphore(d.handle,frame.complete,nullptr);
        if(frame.graphicsReady) d.DestroySemaphore(d.handle,frame.graphicsReady,nullptr);
        if(frame.presentReady) d.DestroySemaphore(d.handle,frame.presentReady,nullptr);
        if(frame.handoffPool) d.DestroyCommandPool(d.handle,frame.handoffPool,nullptr);
        if(frame.framebuffer) d.DestroyFramebuffer(d.handle,frame.framebuffer,nullptr);
        if(frame.viewerDepthView) d.DestroyImageView(d.handle,frame.viewerDepthView,nullptr);
        if(frame.viewerDepth) d.DestroyImage(d.handle,frame.viewerDepth,nullptr);
        if(frame.viewerDepthMemory) d.FreeMemory(d.handle,frame.viewerDepthMemory,nullptr);
        if(frame.probeFramebuffer) d.DestroyFramebuffer(d.handle,frame.probeFramebuffer,nullptr);
        if(frame.probeView) d.DestroyImageView(d.handle,frame.probeView,nullptr);
        if(frame.probeImage) d.DestroyImage(d.handle,frame.probeImage,nullptr);
        if(frame.probeMemory) d.FreeMemory(d.handle,frame.probeMemory,nullptr);
        if(frame.view) d.DestroyImageView(d.handle,frame.view,nullptr);
        frame=Frame{};
    }
    if(swap.pool) d.DestroyCommandPool(d.handle,swap.pool,nullptr);
    if(swap.lines) d.DestroyPipeline(d.handle,swap.lines,nullptr);
    if(swap.fill) d.DestroyPipeline(d.handle,swap.fill,nullptr);
    if(swap.layout) d.DestroyPipelineLayout(d.handle,swap.layout,nullptr);
    if(swap.depthPool) d.DestroyDescriptorPool(d.handle,swap.depthPool,nullptr);
    if(swap.depthSampler) d.DestroySampler(d.handle,swap.depthSampler,nullptr);
    if(swap.depthLayout) d.DestroyDescriptorSetLayout(d.handle,swap.depthLayout,nullptr);
    if(swap.pass) d.DestroyRenderPass(d.handle,swap.pass,nullptr);
    if(swap.probePipeline) d.DestroyPipeline(d.handle,swap.probePipeline,nullptr);
    if(swap.probePass) d.DestroyRenderPass(d.handle,swap.probePass,nullptr);
    swap.pool={}; swap.lines={}; swap.fill={}; swap.layout={}; swap.pass={}; swap.ready=false;
    swap.depthPool={}; swap.depthSampler={}; swap.depthLayout={};
    swap.probePipeline={}; swap.probePass={};
}

static VkPipeline createPipeline(Swapchain& swap,VkShaderModule vertex,VkShaderModule fragment,VkPrimitiveTopology topology,bool blending=true) {
    Device& d=*swap.device;
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(auto& stage:stages) { stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName="main"; }
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex;
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragment;
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32A32_SFLOAT,12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding; vi.vertexAttributeDescriptionCount=2; vi.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=topology;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth=1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable=overlayDepthTestEnabled(topology)?VK_TRUE:VK_FALSE;
    depth.depthWriteEnable=overlayDepthWriteEnabled(topology)?VK_TRUE:VK_FALSE;
    depth.depthCompareOp=topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST?VK_COMPARE_OP_LESS:VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{}; blend.blendEnable=blending?VK_TRUE:VK_FALSE;
    blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.colorBlendOp=VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.alphaBlendOp=VK_BLEND_OP_ADD; blend.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo color{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; color.attachmentCount=1; color.pAttachments=&blend;
    VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamicStates;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount=2; info.pStages=stages; info.pVertexInputState=&vi; info.pInputAssemblyState=&ia; info.pViewportState=&vp; info.pRasterizationState=&raster;
    info.pMultisampleState=&ms; info.pDepthStencilState=&depth; info.pColorBlendState=&color; info.pDynamicState=&dynamic; info.layout=swap.layout; info.renderPass=swap.pass;
    VkPipeline pipeline{}; require(d.CreateGraphicsPipelines(d.handle,VK_NULL_HANDLE,1,&info,nullptr,&pipeline),"CreateGraphicsPipelines failed"); return pipeline;
}

static void createVertexBuffer(Swapchain& swap,Frame& frame,VkDeviceSize capacity) {
    Device& d=*swap.device;
    if(frame.mapped) d.UnmapMemory(d.handle,frame.allocation);
    if(frame.vertices) d.DestroyBuffer(d.handle,frame.vertices,nullptr);
    if(frame.allocation) d.FreeMemory(d.handle,frame.allocation,nullptr);
    frame.mapped=nullptr; frame.vertices={}; frame.allocation={}; frame.vertexCapacity=0;

    const VkDeviceSize probeBytes=VkDeviceSize(swap.probeExtent.width)*swap.probeExtent.height*4;
    const VkDeviceSize smokeBytes=smokeTest()?VkDeviceSize(swap.extent.width)*swap.extent.height*4:0;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size=capacity*sizeof(Vertex)+probeBytes+smokeBytes+6*sizeof(Vertex);
    buffer.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    require(d.CreateBuffer(d.handle,&buffer,nullptr,&frame.vertices),"Create vertex buffer failed");
    VkMemoryRequirements requirements{}; d.GetBufferMemoryRequirements(d.handle,frame.vertices,&requirements);
    uint32_t type=~0u;
    for(uint32_t i=0;i<d.memory.memoryTypeCount;++i) if((requirements.memoryTypeBits&(1u<<i)) &&
        (d.memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type=i; break; }
    if(type==~0u) throw std::runtime_error("No coherent upload memory type");
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize=requirements.size; memory.memoryTypeIndex=type;
    require(d.AllocateMemory(d.handle,&memory,nullptr,&frame.allocation),"Allocate vertex buffer memory failed");
    require(d.BindBufferMemory(d.handle,frame.vertices,frame.allocation,0),"Bind vertex buffer memory failed");
    require(d.MapMemory(d.handle,frame.allocation,0,VK_WHOLE_SIZE,0,&frame.mapped),"Map vertex buffer memory failed");
    frame.vertexCapacity=capacity;
}

static void ensureVertexCapacity(Swapchain& swap,Frame& frame,size_t required) {
    if(required<=frame.vertexCapacity) return;
    if(required>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Visible collision geometry exceeds Vulkan draw vertex count");
    VkDeviceSize capacity=std::max<VkDeviceSize>(frame.vertexCapacity,kInitialVertexCapacity);
    while(capacity<required) {
        if(capacity>std::numeric_limits<VkDeviceSize>::max()/2) { capacity=required; break; }
        capacity*=2;
    }
    createVertexBuffer(swap,frame,capacity);
    char message[180]; sprintf_s(message,"Viewer vertex buffer grew to %llu vertices for visible geometry.",static_cast<unsigned long long>(capacity)); note(message);
}

static void initialize(Swapchain& swap,uint32_t family) {
    Device& d=*swap.device;
    swap.family=family;
    auto getFormatProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(d.instance->gipa(d.instance->handle,"vkGetPhysicalDeviceFormatProperties"));
    if(!getFormatProperties) throw std::runtime_error("Cannot query viewer depth format support");
    for(VkFormat candidate:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{}; getFormatProperties(d.physical,candidate,&properties);
        if(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { swap.viewerDepthFormat=candidate; break; }
    }
    if(swap.viewerDepthFormat==VK_FORMAT_UNDEFINED) throw std::runtime_error("No supported viewer depth format");
    VkAttachmentDescription attachments[2]{};
    attachments[0].format=swap.format; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD; attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; attachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format=swap.viewerDepthFormat; attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&reference; subpass.pDepthStencilAttachment=&depthReference;
    VkSubpassDependency dependency{}; dependency.srcSubpass=VK_SUBPASS_EXTERNAL; dependency.dstSubpass=0;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask=dependency.srcStageMask;
    dependency.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; pass.attachmentCount=2; pass.pAttachments=attachments; pass.subpassCount=1; pass.pSubpasses=&subpass; pass.dependencyCount=1; pass.pDependencies=&dependency;
    require(d.CreateRenderPass(d.handle,&pass,nullptr,&swap.pass),"CreateRenderPass failed");
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; descriptorLayout.bindingCount=1; descriptorLayout.pBindings=&binding;
    require(d.CreateDescriptorSetLayout(d.handle,&descriptorLayout,nullptr,&swap.depthLayout),"Create depth descriptor layout failed");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,static_cast<uint32_t>(swap.frames.size())};
    VkDescriptorPoolCreateInfo descriptorPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; descriptorPool.maxSets=poolSize.descriptorCount; descriptorPool.poolSizeCount=1; descriptorPool.pPoolSizes=&poolSize;
    require(d.CreateDescriptorPool(d.handle,&descriptorPool,nullptr,&swap.depthPool),"Create depth descriptor pool failed");
    std::vector<VkDescriptorSetLayout> layouts(swap.frames.size(),swap.depthLayout);
    std::vector<VkDescriptorSet> sets(swap.frames.size());
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; setInfo.descriptorPool=swap.depthPool; setInfo.descriptorSetCount=static_cast<uint32_t>(sets.size()); setInfo.pSetLayouts=layouts.data();
    require(d.AllocateDescriptorSets(d.handle,&setInfo,sets.data()),"Allocate depth descriptor sets failed");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST; sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sampler.maxLod=0;
    require(d.CreateSampler(d.handle,&sampler,nullptr,&swap.depthSampler),"Create depth sampler failed");
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; layout.pushConstantRangeCount=1; layout.pPushConstantRanges=&range;
    layout.setLayoutCount=1; layout.pSetLayouts=&swap.depthLayout;
    require(d.CreatePipelineLayout(d.handle,&layout,nullptr,&swap.layout),"CreatePipelineLayout failed");
    VkShaderModule vertex{},fragment{};
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader.codeSize=sizeof(kVertexShader); shader.pCode=kVertexShader;
    require(d.CreateShaderModule(d.handle,&shader,nullptr,&vertex),"Create vertex shader failed");
    shader.codeSize=sizeof(kFragmentShader); shader.pCode=kFragmentShader;
    VkResult fragmentResult=d.CreateShaderModule(d.handle,&shader,nullptr,&fragment);
    if(fragmentResult!=VK_SUCCESS) { d.DestroyShaderModule(d.handle,vertex,nullptr); require(fragmentResult,"Create fragment shader failed"); }
    try { swap.lines=createPipeline(swap,vertex,fragment,VK_PRIMITIVE_TOPOLOGY_LINE_LIST); swap.fill=createPipeline(swap,vertex,fragment,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST); }
    catch(...) { d.DestroyShaderModule(d.handle,vertex,nullptr); d.DestroyShaderModule(d.handle,fragment,nullptr); throw; }
    d.DestroyShaderModule(d.handle,vertex,nullptr); d.DestroyShaderModule(d.handle,fragment,nullptr);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex=family;
    require(d.CreateCommandPool(d.handle,&pool,nullptr,&swap.pool),"CreateCommandPool failed");
    std::vector<VkCommandBuffer> buffers(swap.frames.size());
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool=swap.pool; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount=static_cast<uint32_t>(buffers.size());
    require(d.AllocateCommandBuffers(d.handle,&allocate,buffers.data()),"AllocateCommandBuffers failed");
    swap.probeExtent={256,std::max(1u,static_cast<uint32_t>(256ull*swap.extent.height/swap.extent.width))};
    for(size_t i=0;i<swap.frames.size();++i) {
        Frame& frame=swap.frames[i]; frame.commands=buffers[i]; frame.depthSet=sets[i];
        if(d.setLoaderData) require(d.setLoaderData(d.handle,frame.commands),"SetDeviceLoaderData failed");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=frame.image; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=swap.format;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        require(d.CreateImageView(d.handle,&view,nullptr,&frame.view),"CreateImageView failed");
        VkImageCreateInfo depthImage{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; depthImage.imageType=VK_IMAGE_TYPE_2D; depthImage.format=swap.viewerDepthFormat;
        depthImage.extent={swap.extent.width,swap.extent.height,1}; depthImage.mipLevels=1; depthImage.arrayLayers=1; depthImage.samples=VK_SAMPLE_COUNT_1_BIT;
        depthImage.tiling=VK_IMAGE_TILING_OPTIMAL; depthImage.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; depthImage.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        require(d.CreateImage(d.handle,&depthImage,nullptr,&frame.viewerDepth),"Create viewer depth image failed");
        VkMemoryRequirements depthRequirements{}; d.GetImageMemoryRequirements(d.handle,frame.viewerDepth,&depthRequirements);
        uint32_t depthType=~0u;
        for(uint32_t j=0;j<d.memory.memoryTypeCount;++j) if((depthRequirements.memoryTypeBits&(1u<<j))&&(d.memory.memoryTypes[j].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { depthType=j; break; }
        if(depthType==~0u) for(uint32_t j=0;j<d.memory.memoryTypeCount;++j) if(depthRequirements.memoryTypeBits&(1u<<j)) { depthType=j; break; }
        if(depthType==~0u) throw std::runtime_error("No memory type for viewer depth image");
        VkMemoryAllocateInfo depthMemory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; depthMemory.allocationSize=depthRequirements.size; depthMemory.memoryTypeIndex=depthType;
        require(d.AllocateMemory(d.handle,&depthMemory,nullptr,&frame.viewerDepthMemory),"Allocate viewer depth memory failed");
        require(d.BindImageMemory(d.handle,frame.viewerDepth,frame.viewerDepthMemory,0),"Bind viewer depth memory failed");
        VkImageViewCreateInfo depthView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; depthView.image=frame.viewerDepth; depthView.viewType=VK_IMAGE_VIEW_TYPE_2D; depthView.format=swap.viewerDepthFormat;
        depthView.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        require(d.CreateImageView(d.handle,&depthView,nullptr,&frame.viewerDepthView),"Create viewer depth image view failed");
        VkImageView framebufferViews[]={frame.view,frame.viewerDepthView};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass=swap.pass; framebuffer.attachmentCount=2; framebuffer.pAttachments=framebufferViews;
        framebuffer.width=swap.extent.width; framebuffer.height=swap.extent.height; framebuffer.layers=1;
        require(d.CreateFramebuffer(d.handle,&framebuffer,nullptr,&frame.framebuffer),"CreateFramebuffer failed");
        createVertexBuffer(swap,frame,kInitialVertexCapacity);
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; require(d.CreateSemaphore(d.handle,&semaphore,nullptr,&frame.complete),"CreateSemaphore failed");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        require(d.CreateFence(d.handle,&fence,nullptr,&frame.fence),"CreateFence failed");
    }
    swap.ready=true; if(sceneTest()) loadScene(); note("Independent Vulkan renderer initialized.");
}

static bool smokeTest() { static const bool value=GetEnvironmentVariableW(L"DCV_SMOKE_TEST",nullptr,0)>0; return value; }
static uint32_t occlusionCase() { static const uint32_t value=[] { wchar_t text[32]{}; GetEnvironmentVariableW(L"DCV_OCCLUSION_TEST",text,32); return text[0]=='h'?1u:(text[0]=='p'?2u:(text[0]=='f'?3u:(text[0]=='r'?4u:(text[0]=='i'?(text[9]=='p'?6u:(text[9]=='f'?7u:5u)):0u)))); }(); return value; }
static void updateViewerSettings() {
    static ULONGLONG lastRead=0;
    static std::wstring settingsPath=[] {
        wchar_t localAppData[32768]{};
        constexpr DWORD capacity=32768;
        DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",localAppData,capacity);
        if(length==0||length>=capacity) return std::wstring{};
        return std::wstring(localAppData)+L"\\DEViL\\viewer.ini";
    }();
    const ULONGLONG now=GetTickCount64();
    if(settingsPath.empty()||now-lastRead<250) return;
    lastRead=now;

    const bool oldEnabled=enabled, oldTriggers=showTriggers, oldCollision=showCollision, oldWalls=showWalls;
    const uint32_t oldMode=collisionMode;
    const float oldRadius=radius;
    enabled=GetPrivateProfileIntW(L"Viewer",L"Enabled",enabled?1:0,settingsPath.c_str())!=0;
    showTriggers=GetPrivateProfileIntW(L"Viewer",L"ShowTriggers",showTriggers?1:0,settingsPath.c_str())!=0;
    showCollision=GetPrivateProfileIntW(L"Viewer",L"ShowCollision",showCollision?1:0,settingsPath.c_str())!=0;
    showWalls=GetPrivateProfileIntW(L"Viewer",L"ShowWalls",showWalls?1:0,settingsPath.c_str())!=0;
    collisionMode=GetPrivateProfileIntW(L"Viewer",L"CollisionMode",static_cast<int>(collisionMode),settingsPath.c_str())!=0?1u:0u;
    radius=static_cast<float>(std::clamp(GetPrivateProfileIntW(L"Viewer",L"Distance",static_cast<UINT>(radius),settingsPath.c_str()),UINT{20},UINT{200}));
    if(oldEnabled!=enabled||oldTriggers!=showTriggers||oldCollision!=showCollision||oldWalls!=showWalls||oldMode!=collisionMode||oldRadius!=radius) {
        char message[200];
        sprintf_s(message,"DEViL settings: triggers=%d collision=%d walls=%d fill=%u radius=%.0f enabled=%d",showTriggers,showCollision,showWalls,collisionMode,radius,enabled);
        note(message);
    }
}

struct ViewFilter {
    float planes[5][4]; const GameCamera& camera;
    explicit ViewFilter(const GameCamera& view):camera(view) {
        for(int plane=0;plane<5;++plane) for(int k=0;k<4;++k)
            planes[plane][k]=view.matrix[12+k]+(plane==4?0.0f:((plane&1)?-1.0f:1.0f)*view.matrix[(plane/2)*4+k]);
    }
    float distanceSquared(const float* bounds) const {
        float result=0;
        for(int axis=0;axis<3;++axis) { float d=std::max(bounds[axis]-camera.position[axis],std::max(0.0f,camera.position[axis]-bounds[axis+3])); result+=d*d; }
        return result;
    }
    float screenDistanceSquared(const float* bounds) const {
        float minimumX=FLT_MAX,minimumY=FLT_MAX,maximumX=-FLT_MAX,maximumY=-FLT_MAX;
        for(uint32_t corner=0;corner<8;++corner) {
            float point[4]{};
            for(int axis=0;axis<3;++axis) point[axis]=(corner&(1u<<axis))?bounds[axis+3]:bounds[axis];
            point[3]=1.0f;
            float x=0,y=0,w=0;
            for(int axis=0;axis<4;++axis) {
                x+=camera.matrix[axis]*point[axis];
                y+=camera.matrix[4+axis]*point[axis];
                w+=camera.matrix[12+axis]*point[axis];
            }
            // A bounds box crossing the camera plane can cover the center, so
            // conservatively give it center priority.
            if(w<=.001f) return 0.0f;
            float screenX=x/w,screenY=-y/w;
            if(!std::isfinite(screenX)||!std::isfinite(screenY)) return 0.0f;
            minimumX=std::min(minimumX,screenX); maximumX=std::max(maximumX,screenX);
            minimumY=std::min(minimumY,screenY); maximumY=std::max(maximumY,screenY);
        }
        float dx=minimumX>0?minimumX:(maximumX<0?-maximumX:0.0f);
        float dy=minimumY>0?minimumY:(maximumY<0?-maximumY:0.0f);
        return dx*dx+dy*dy;
    }
    bool inView(const float* bounds) const {
        for(const auto& p:planes) {
            float maximum=p[3];
            for(int axis=0;axis<3;++axis) maximum+=p[axis]*(p[axis]>=0?bounds[axis+3]:bounds[axis]);
            if(maximum<0) return false;
        }
        return true;
    }
};

struct Candidate { const Mesh* mesh; float distance; float screenDistance=0; };
static uint32_t candidatePriority(uint32_t kind) { return kind==1?0u:(kind==0?1u:2u); }
static void sortVisibleCandidates(std::vector<Candidate>& visible) {
    std::sort(visible.begin(),visible.end(),[](const Candidate& a,const Candidate& b) {
        uint32_t priorityA=candidatePriority(a.mesh->kind),priorityB=candidatePriority(b.mesh->kind);
        if(priorityA!=priorityB) return priorityA<priorityB;
        if(a.screenDistance!=b.screenDistance) return a.screenDistance<b.screenDistance;
        if(a.distance!=b.distance) return a.distance<b.distance;
        return std::less<const Mesh*>{}(a.mesh,b.mesh);
    });
}
#include "live_walls.h"
#include "game_ui.h"
#include "runtime_scene.h"
#include "console_clip.h"
static uint32_t gatherVisible(const GameCamera& camera,std::vector<Candidate>& visible) {
    visible.clear();
    ViewFilter filter(camera);
    uint32_t examined=0;
    static const std::vector<Mesh> emptyMeshes;
    static const std::vector<SpatialNode> emptyNodes;
    static const std::vector<uint32_t> emptyOrder;
    const auto& source=runtimeRendering?(runtimeWorld?runtimeWorld->meshes:emptyMeshes):meshes;
    const auto& nodes=runtimeRendering?(runtimeWorld?runtimeWorld->nodes:emptyNodes):spatialNodes;
    const auto& order=runtimeRendering?(runtimeWorld?runtimeWorld->order:emptyOrder):spatialOrder;
    const float selectionRadius=cullingRadius();
    auto visit=[&](auto&& self,uint32_t index)->void {
        const SpatialNode& node=nodes[index];
        if(filter.distanceSquared(node.bounds)>selectionRadius*selectionRadius||!filter.inView(node.bounds)) return;
        if(node.left!=~0u) { self(self,node.left); self(self,node.right); return; }
        for(uint32_t i=node.begin;i<node.begin+node.count;++i) {
            ++examined;
            const Mesh& mesh=source[order[i]];
            if(sceneTest()&&mesh.kind==0) continue;
            if((mesh.kind==0&&!showTriggers)||(mesh.kind==1&&!showCollision)) continue;
            float distance=filter.distanceSquared(mesh.bounds);
            if(distance<=selectionRadius*selectionRadius&&filter.inView(mesh.bounds)) visible.push_back({&mesh,distance,filter.screenDistanceSquared(mesh.bounds)});
        }
    };
    if(!nodes.empty()) visit(visit,0);
    const auto& live=runtimeRendering?(runtimeEntities?runtimeEntities->meshes:emptyMeshes):liveWalls.meshes;
    for(const Mesh& mesh:live) {
        if((mesh.kind==0&&!showTriggers)||(mesh.kind==2&&!showWalls)) continue;
        float distance=filter.distanceSquared(mesh.bounds);
        if(distance<=selectionRadius*selectionRadius&&filter.inView(mesh.bounds)) visible.push_back({&mesh,distance,filter.screenDistanceSquared(mesh.bounds)});
    }
    return examined;
}

static uint32_t collectVertices(const GameCamera& camera,std::vector<Vertex>& output) {
    auto started=std::chrono::steady_clock::now();
    output.clear();
    auto append=[&](Vec3 position,const std::array<float,4>& color) { Vertex vertex{}; vertex.position=position; memcpy(vertex.color,color.data(),16); output.push_back(vertex); };
    if(occlusionCase()) {
        float z=(occlusionCase()==3||occlusionCase()==7)?.5f:2.0f;
        const std::array<float,4> green{.25f,.95f,.12f,1},cyan{.1f,.85f,1,1};
        append({-.6f*z,-.6f*z,z},green); append({.6f*z,-.6f*z,z},green); append({.6f*z,.6f*z,z},green);
        append({-.6f*z,-.6f*z,z},green); append({.6f*z,.6f*z,z},green); append({-.6f*z,.6f*z,z},green);
        append({-.6f*z,.75f*z,z},cyan); append({.6f*z,.75f*z,z},cyan);
        return 6;
    }
    if(smokeTest()&&!sceneTest()) {
        append({-.6f,-.6f,0},{1,0,0,1}); append({.6f,-.6f,0},{0,1,0,1}); append({0,.6f,0},{0,0,1,1});
        append({-.8f,0,0},{1,1,1,1}); append({.8f,0,0},{1,1,1,1}); return 3;
    }
    if(sceneTest()) {
        static const uint32_t testMode=[] { wchar_t value[8]{}; GetEnvironmentVariableW(L"DCV_TEST_COLLISION_MODE",value,8); return value[0]=='1'?1u:0u; }();
        collisionMode=testMode;
    }
    static std::vector<Candidate> visible;
    uint32_t examined=gatherVisible(camera,visible);
    // Prioritize geometry nearest the screen center, then nearer objects. The
    // GPU upload buffer grows on demand, so this ordering does not omit edges.
    sortVisibleCandidates(visible);
    uint32_t collisionTriangleVertices=0,collisionFilledMeshes=0;
    for(const Candidate& candidate:visible) {
        const Mesh* mesh=candidate.mesh;
        if(!collisionMode) continue;
        const std::array<float,4> color=mesh->kind==0?std::array<float,4>{0.15f,0.8f,1.0f,0.09f}:(mesh->kind==1?std::array<float,4>{0.25f,0.95f,0.12f,0.09f}:std::array<float,4>{1.0f,0.45f,0.05f,0.09f});
        for(Vec3 point:mesh->geometry->triangles) append(mesh->world(point),color);
        if(mesh->kind==1) { collisionTriangleVertices+=static_cast<uint32_t>(mesh->geometry->triangles.size()); ++collisionFilledMeshes; }
    }
    if(output.size()>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Visible collision fill exceeds Vulkan draw vertex count");
    uint32_t triangles=static_cast<uint32_t>(output.size());
    uint32_t drawn[3]{},segments[3]{};
    for(const Candidate& candidate:visible) {
        const Mesh* mesh=candidate.mesh;
        std::array<float,4> color=mesh->kind==0?std::array<float,4>{0.1f,0.85f,1.0f,0.9f}:(mesh->kind==1?std::array<float,4>{0.4f,1.0f,0.2f,0.65f}:std::array<float,4>{1.0f,0.45f,0.05f,0.9f});
        for(Vec3 point:mesh->geometry->lines) append(mesh->world(point),color);
        ++drawn[mesh->kind]; segments[mesh->kind]+=static_cast<uint32_t>(mesh->geometry->lines.size()/2);
    }
    static ULONGLONG lastReport=0;
    if(output.size()>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Visible collision geometry exceeds Vulkan draw vertex count");
    if(GetTickCount64()-lastReport>5000) {
        double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        size_t total=runtimeRendering?(runtimeWorld?runtimeWorld->meshes.size():0):meshes.size();
        char message[520]; sprintf_s(message,"Geometry: triggers=%u (%u edges), walls=%u (%u edges), collisionWire=%u (%u edges), collisionFill=%u (%u triangles), candidates=%zu, examined=%u/%zu prepare=%.3fms radius=%.0f camera=(%.2f,%.2f,%.2f)",drawn[0],segments[0],drawn[2],segments[2],drawn[1],segments[1],collisionFilledMeshes,collisionTriangleVertices/3,visible.size(),examined,total,elapsed,radius,camera.position[0],camera.position[1],camera.position[2]); note(message);
        lastReport=GetTickCount64();
    }
    return triangles;
}

#include "depth_sampling.h"
#include "depth_probe.h"
#include "presentation_bridge.h"

static bool draw(Swapchain& swap,VkQueue queue,uint32_t imageIndex,const VkPresentInfoKHR& present,VkQueue presentQueue,uint32_t presentFamily) {
    Device& d=*swap.device;
    swap.submitFailure=VK_SUCCESS;
    if(imageIndex>=swap.frames.size()) return false;
    const GameProfile* profile=currentGameProfile();
    uintptr_t gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    GameCamera camera{};
    if(occlusionCase()) { camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=camera.matrix[14]=1; camera.matrix[11]=-.05f; }
    else if(sceneTest()) {
        std::ifstream input(folder/L"test-camera.bin",std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(&camera),sizeof(camera))) throw std::runtime_error("Missing test camera");
    }
    else if(smokeTest()) { camera.matrix[0]=camera.matrix[5]=camera.matrix[10]=camera.matrix[15]=1; }
    else if(!profile||!readGameCamera(WallMemory{},gameBase,*profile,camera)) {
        static ULONGLONG last=0;
        if(GetTickCount64()-last>5000) { note(profile?"Waiting for a valid supported level camera.":"No supported DOOM Eternal 1.0/1.1/3.1/5.1 executable profile detected; overlay withheld."); last=GetTickCount64(); }
        return false;
    }
    if(!smokeTest()&&!sceneTest()&&!occlusionCase()) {
        static bool reported=false;
        if(!reported) { char message[180]; sprintf_s(message,"Executable profile selected: %s; shared physical collision, visibility, menu and console filters enabled.",profile->name); note(message); reported=true; }
        bool playing=gameplayVisible(WallMemory{},gameBase,*profile);
        static int previous=-1;
        if(previous!=int(playing)) { note(playing?"Gameplay resumed: overlay visible.":"Menu/pause/loading: overlay hidden."); previous=int(playing); }
        if(!playing) return false;
    }
    DepthImage* screenDepth=chooseDepth(swap);
    if(!screenDepth) return false;
    if(!smokeTest()&&!sceneTest()&&!occlusionCase()&&!updateRuntimeScene()) return false;
    float consoleFraction=smokeTest()||sceneTest()||occlusionCase()?0.0f:readConsoleFraction(WallMemory{},gameBase,*profile);
    if(smokeTest()) { wchar_t value[32]{}; if(GetEnvironmentVariableW(L"DCV_TEST_CONSOLE_FRACTION",value,32)) consoleFraction=static_cast<float>(_wtof(value)); }
    uint32_t consoleTop=consoleClipTop(consoleFraction,swap.extent.height);
    if(consoleTop==swap.extent.height&&!smokeTest()) return false;
    static std::vector<Vertex> vertices;
    uint32_t triangleVertices=collectVertices(camera,vertices);
    if(vertices.empty()) return false;
    Frame& frame=swap.frames[imageIndex];
    if(d.WaitForFences(d.handle,1,&frame.fence,VK_TRUE,1000000000ull)!=VK_SUCCESS) { note("Overlay frame fence timeout; frame skipped."); return false; }
    const bool ownershipHandoff=presentFamily!=swap.family&&swap.sharing==VK_SHARING_MODE_EXCLUSIVE;
    if(ownershipHandoff) preparePresentHandoff(swap,frame,presentFamily);
    finishDepthProbe(swap,frame);
    ensureVertexCapacity(swap,frame,vertices.size());
    memcpy(frame.mapped,vertices.data(),vertices.size()*sizeof(Vertex));
    if(frame.boundDepth!=screenDepth->handle) {
        VkDescriptorImageInfo image{swap.depthSampler,screenDepth->sampleView,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet=frame.depthSet; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&image;
        d.UpdateDescriptorSets(d.handle,1,&write,0,nullptr); frame.boundDepth=screenDepth->handle;
    }
    require(d.ResetCommandBuffer(frame.commands,0),"ResetCommandBuffer failed");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    require(d.BeginCommandBuffer(frame.commands,&begin),"BeginCommandBuffer failed");
    if(ownershipHandoff) swapOwnershipBarrier(d,frame.commands,frame.image,presentFamily,swap.family,true,true);
    depthSamplingBarrier(d,frame.commands,*screenDepth,false);
    struct PushConstants { float matrix[16]; float options[4]; float depth[4]; float uv[4]; float debug[4]; } push{};
    memcpy(push.matrix,camera.matrix,64); push.options[0]=1.0e30f; // shader far plane; radius filters whole mesh bounds on CPU
    float forwardSquared=0,dot=0; for(int k=0;k<3;++k) { forwardSquared+=camera.matrix[12+k]*camera.matrix[12+k]; dot+=camera.matrix[8+k]*camera.matrix[12+k]; }
    push.options[1]=forwardSquared>0?dot/forwardSquared:1; push.options[2]=camera.matrix[11]-push.options[1]*camera.matrix[15];
    push.options[3]=smokeTest()&&!sceneTest()&&!occlusionCase()?0.0f:1.0f;
    push.depth[0]=screenDepth->viewport.minDepth; push.depth[1]=screenDepth->viewport.maxDepth; push.depth[2]=static_cast<float>(visibilitySettings.mode); push.depth[3]=visibilitySettings.bias;
    if(occlusionCase()>=5) push.depth[2]=2;
    push.uv[0]=fabsf(screenDepth->viewport.width)/screenDepth->extent.width/swap.extent.width;
    push.uv[1]=fabsf(screenDepth->viewport.height)/screenDepth->extent.height/swap.extent.height;
    push.uv[2]=std::min(screenDepth->viewport.x,screenDepth->viewport.x+screenDepth->viewport.width)/screenDepth->extent.width;
    push.uv[3]=std::min(screenDepth->viewport.y,screenDepth->viewport.y+screenDepth->viewport.height)/screenDepth->extent.height;
    if(visibilitySettings.probe>swap.lastProbe) recordDepthProbe(swap,frame,*screenDepth,camera,push);
    VkClearValue clearValues[2]{}; clearValues[1].depthStencil={1.0f,0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; pass.renderPass=swap.pass; pass.framebuffer=frame.framebuffer; pass.renderArea.extent=swap.extent;
    pass.clearValueCount=2; pass.pClearValues=clearValues;
    d.CmdBeginRenderPass(frame.commands,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,static_cast<float>(swap.extent.width),static_cast<float>(swap.extent.height),0,1};
    VkRect2D scissor{{0,static_cast<int32_t>(consoleTop)},{swap.extent.width,swap.extent.height-consoleTop}}; d.CmdSetViewport(frame.commands,0,1,&viewport); d.CmdSetScissor(frame.commands,0,1,&scissor);
    VkDeviceSize offset=0; d.CmdBindVertexBuffers(frame.commands,0,1,&frame.vertices,&offset);
    d.CmdPushConstants(frame.commands,swap.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    d.CmdBindDescriptorSets(frame.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,swap.layout,0,1,&frame.depthSet,0,nullptr);
    if(triangleVertices) {
        auto fillPush=push; fillPush.depth[3]=fillDepthBias(push.depth[3]);
        d.CmdPushConstants(frame.commands,swap.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(fillPush),&fillPush);
        d.CmdBindPipeline(frame.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,swap.fill); d.CmdDraw(frame.commands,triangleVertices,1,0,0);
        d.CmdPushConstants(frame.commands,swap.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    }
    uint32_t lineVertices=static_cast<uint32_t>(vertices.size())-triangleVertices;
    if(lineVertices) { d.CmdBindPipeline(frame.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,swap.lines); d.CmdDraw(frame.commands,lineVertices,1,triangleVertices,0); }
    d.CmdEndRenderPass(frame.commands);
    depthSamplingBarrier(d,frame.commands,*screenDepth,true);
    const VkDeviceSize readbackOffset=frame.vertexCapacity*sizeof(Vertex);
    if(smokeTest()) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.image=frame.image; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        d.CmdPipelineBarrier(frame.commands,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy region{}; region.bufferOffset=readbackOffset; region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent={swap.extent.width,swap.extent.height,1};
        d.CmdCopyImageToBuffer(frame.commands,frame.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,frame.vertices,1,&region);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=0; barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        d.CmdPipelineBarrier(frame.commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,1,&host,0,nullptr,1,&barrier);
    }
    if(ownershipHandoff) swapOwnershipBarrier(d,frame.commands,frame.image,swap.family,presentFamily,false,true);
    require(d.EndCommandBuffer(frame.commands),"EndCommandBuffer failed");
    require(d.ResetFences(d.handle,1,&frame.fence),"ResetFences failed");
    std::vector<VkPipelineStageFlags> stages(present.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    if(ownershipHandoff) {
        VkSubmitInfo handoff{VK_STRUCTURE_TYPE_SUBMIT_INFO}; handoff.waitSemaphoreCount=present.waitSemaphoreCount; handoff.pWaitSemaphores=present.pWaitSemaphores; handoff.pWaitDstStageMask=stages.data();
        handoff.commandBufferCount=1; handoff.pCommandBuffers=&frame.releaseToGraphics; handoff.signalSemaphoreCount=1; handoff.pSignalSemaphores=&frame.graphicsReady;
        swap.submitFailure=d.QueueSubmit(presentQueue,1,&handoff,VK_NULL_HANDLE);
        if(swap.submitFailure!=VK_SUCCESS) { note("Presentation release submission failed."); return false; }
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount=present.waitSemaphoreCount; submit.pWaitSemaphores=present.pWaitSemaphores;
    submit.pWaitDstStageMask=stages.data(); submit.commandBufferCount=1; submit.pCommandBuffers=&frame.commands; submit.signalSemaphoreCount=1; submit.pSignalSemaphores=&frame.complete;
    VkPipelineStageFlags graphicsWait=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if(ownershipHandoff) { submit.waitSemaphoreCount=1; submit.pWaitSemaphores=&frame.graphicsReady; submit.pWaitDstStageMask=&graphicsWait; }
    VkResult result=d.QueueSubmit(queue,1,&submit,ownershipHandoff?VK_NULL_HANDLE:frame.fence);
    if(result!=VK_SUCCESS) { swap.submitFailure=result; enabled=false; note("Overlay QueueSubmit failed; overlay disabled."); return false; }
    if(ownershipHandoff) {
        VkSubmitInfo handoff{VK_STRUCTURE_TYPE_SUBMIT_INFO}; handoff.waitSemaphoreCount=1; handoff.pWaitSemaphores=&frame.complete; handoff.pWaitDstStageMask=&graphicsWait;
        handoff.commandBufferCount=1; handoff.pCommandBuffers=&frame.acquireForPresent; handoff.signalSemaphoreCount=1; handoff.pSignalSemaphores=&frame.presentReady;
        swap.submitFailure=d.QueueSubmit(presentQueue,1,&handoff,frame.fence);
        if(swap.submitFailure!=VK_SUCCESS) { note("Presentation acquire submission failed."); return false; }
    }
    if(smokeTest()&&d.WaitForFences(d.handle,1,&frame.fence,VK_TRUE,1000000000ull)==VK_SUCCESS) {
        const unsigned char* pixels=static_cast<const unsigned char*>(frame.mapped)+readbackOffset;
        const unsigned char* pixel=pixels+(size_t(swap.extent.height/2)*swap.extent.width+swap.extent.width/2)*4;
        uint32_t green=0,cyan=0,leftColored=0,rightColored=0,consoleLeak=0,belowConsole=0;
        std::unordered_set<uint32_t> fillColors;
        for(size_t i=0;i<size_t(swap.extent.width)*swap.extent.height;++i) {
            bool isGreen=pixels[i*4+1]>30&&pixels[i*4+1]>pixels[i*4]*1.3f&&pixels[i*4+1]>pixels[i*4+2]*1.3f;
            bool isCyan=pixels[i*4+1]>100&&pixels[i*4+1]>pixels[i*4]*1.8f&&pixels[i*4+2]>pixels[i*4]*1.8f;
            if(isGreen) { ++green; fillColors.insert(uint32_t(pixels[i*4])|(uint32_t(pixels[i*4+1])<<8)|(uint32_t(pixels[i*4+2])<<16)); }
            if(isCyan) ++cyan;
            if(pixels[i*4]>10||pixels[i*4+1]>10||pixels[i*4+2]>10) { if(i/swap.extent.width<consoleTop) ++consoleLeak; else ++belowConsole; }
            if(isGreen||isCyan) { if(i%swap.extent.width<swap.extent.width/2) ++leftColored; else ++rightColored; }
        }
        bool passed=sceneTest()?green>0:(pixel[0]>10||pixel[1]>10||pixel[2]>10);
        if(occlusionCase()==1||occlusionCase()==5) passed=green==0&&cyan==0;
        if(occlusionCase()==2||occlusionCase()==6) passed=green>0&&cyan>0&&leftColored==0&&rightColored>0&&fillColors.size()==1;
        if(occlusionCase()==3||occlusionCase()==7) passed=green>0&&cyan>0&&leftColored>0&&rightColored>0&&fillColors.size()==1;
        if(occlusionCase()==4) passed=green==0&&cyan==0;
        if(consoleTop) passed=consoleLeak==0&&(consoleTop==swap.extent.height?belowConsole==0:belowConsole>0);
        FILE* report=_wfsopen((folder/L"smoke-result.json").c_str(),L"w",_SH_DENYNO);
        if(report) { fprintf(report,"{\"rendered\":%s,\"collisionSceneTest\":%s,\"occlusionCase\":%u,\"greenPixels\":%u,\"cyanPixels\":%u,\"leftColored\":%u,\"rightColored\":%u,\"uniqueFillColors\":%zu,\"consoleTop\":%u,\"consoleLeakPixels\":%u,\"belowConsolePixels\":%u,\"pixel\":[%u,%u,%u,%u],\"triangleVertices\":%u,\"lineVertices\":%u}\n",passed?"true":"false",sceneTest()?"true":"false",occlusionCase(),green,cyan,leftColored,rightColored,fillColors.size(),consoleTop,consoleLeak,belowConsole,pixel[0],pixel[1],pixel[2],pixel[3],triangleVertices,lineVertices); fclose(report); }
        if(sceneTest()||occlusionCase()) {
            std::ofstream output(folder/L"collision-frame.ppm",std::ios::binary);
            output<<"P6\n"<<swap.extent.width<<" "<<swap.extent.height<<"\n255\n";
            bool bgra=swap.format==VK_FORMAT_B8G8R8A8_UNORM||swap.format==VK_FORMAT_B8G8R8A8_SRGB;
            for(size_t i=0;i<size_t(swap.extent.width)*swap.extent.height;++i) { char rgb[3]={static_cast<char>(pixels[i*4+(bgra?2:0)]),static_cast<char>(pixels[i*4+1]),static_cast<char>(pixels[i*4+(bgra?0:2)])}; output.write(rgb,3); }
        }
        note(passed?"GPU readback passed: overlay pixels are present.":"GPU readback failed: overlay pixel is black.");
    }
    ++swap.rendered;
    if(swap.rendered==1 || swap.rendered%600==0) {
        char message[240]; sprintf_s(message,"Drawn frames=%llu filled triangles=%u line segments=%u camera=(%.2f,%.2f,%.2f)",swap.rendered,triangleVertices/3,lineVertices/2,camera.position[0],camera.position[1],camera.position[2]); note(message);
    }
    return true;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL physicalProcAddr(VkInstance instance,const char* name) {
    std::lock_guard<std::recursive_mutex> lock(guard);
    auto found=instances.find(key(instance)); if(found==instances.end()) return nullptr;
    return found->second->physicalGipa?found->second->physicalGipa(instance,name):found->second->gipa(instance,name);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* allocator,VkInstance* instance) {
    std::lock_guard<std::recursive_mutex> lock(guard); note("Collision Vulkan layer loaded. No ReShade dependency.");
    auto chain=reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while(chain && !(chain->sType==VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO&&chain->function==VK_LAYER_LINK_INFO)) chain=reinterpret_cast<const VkLayerInstanceCreateInfo*>(chain->pNext);
    if(!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto next=chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto nextPhysical=chain->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    auto create=reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE,"vkCreateInstance"));
    const_cast<VkLayerInstanceCreateInfo*>(chain)->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    VkResult result=create(info,allocator,instance);
    if(result==VK_SUCCESS) {
        auto data=std::make_unique<Instance>(); data->handle=*instance; data->gipa=next; data->physicalGipa=nextPhysical;
        data->memoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(next(*instance,"vkGetPhysicalDeviceMemoryProperties"));
        data->queueProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(next(*instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        instances[key(*instance)]=std::move(data);
    }
    return result;
}

EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); void* handleKey=key(instance);
    auto found=instances.find(handleKey); if(found==instances.end()) return;
    runtimeManager.stop(); runtimeWorld.reset(); runtimeEntities.reset(); runtimeLevel={}; runtimeRendering=false;
    auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(found->second->gipa(instance,"vkDestroyInstance")); destroy(instance,allocator); instances.erase(handleKey);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* allocator,VkDevice* device) {
    std::lock_guard<std::recursive_mutex> lock(guard);
    auto owner=instances.find(key(physical)); if(owner==instances.end()) return VK_ERROR_INITIALIZATION_FAILED;
    auto chain=reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    PFN_vkSetDeviceLoaderData setData=nullptr;
    for(auto node=chain;node;node=reinterpret_cast<const VkLayerDeviceCreateInfo*>(node->pNext)) if(node->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO&&node->function==VK_LOADER_DATA_CALLBACK) setData=node->u.pfnSetDeviceLoaderData;
    while(chain&&!(chain->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO&&chain->function==VK_LAYER_LINK_INFO)) chain=reinterpret_cast<const VkLayerDeviceCreateInfo*>(chain->pNext);
    if(!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto next=chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto create=reinterpret_cast<PFN_vkCreateDevice>(chain->u.pLayerInfo->pfnNextGetInstanceProcAddr(owner->second->handle,"vkCreateDevice"));
    const_cast<VkLayerDeviceCreateInfo*>(chain)->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    VkResult result=create(physical,info,allocator,device);
    if(result==VK_SUCCESS) {
        auto data=std::make_unique<Device>(); data->handle=*device; data->physical=physical; data->instance=owner->second.get(); data->gdpa=next; data->setLoaderData=setData;
#define LOAD(name) data->name=reinterpret_cast<PFN_vk##name>(next(*device,"vk" #name));
        DEVICE_FUNCTIONS(LOAD)
        DEPTH_FUNCTIONS(LOAD)
#undef LOAD
        data->instance->memoryProperties(physical,&data->memory);
        uint32_t count=0; data->instance->queueProperties(physical,&count,nullptr); data->families.resize(count); data->instance->queueProperties(physical,&count,data->families.data());
        devices[key(*device)]=std::move(data); note("Vulkan device connected.");
    }
    return result;
}

EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); void* handleKey=key(device); Device* data=findDevice(device); if(!data) return;
    data->DeviceWaitIdle(device);
    for(auto i=swaps.begin();i!=swaps.end();) if(i->second->device==data) { cleanup(*i->second); i=swaps.erase(i); } else ++i;
    for(auto i=queues.begin();i!=queues.end();) if(i->second.device==data) i=queues.erase(i); else ++i;
    for(auto& item:data->depth.images) if(item.second.sampleView) data->DestroyImageView(device,item.second.sampleView,nullptr);
    data->DestroyDevice(device,allocator); devices.erase(handleKey);
}

EXPORT VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue* queue) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device* data=findDevice(device); data->GetDeviceQueue(device,family,index,queue); queues[*queue]={data,family};
}
EXPORT VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice device,const VkDeviceQueueInfo2* info,VkQueue* queue) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device* data=findDevice(device); data->GetDeviceQueue2(device,info,queue); queues[*queue]={data,info->queueFamilyIndex};
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* allocator,VkSwapchainKHR* swapchain) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device* data=findDevice(device);
    VkSwapchainCreateInfoKHR modified=*info;
    auto capabilities=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(data->instance->gipa(data->instance->handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    VkSurfaceCapabilitiesKHR caps{};
    if(capabilities&&capabilities(data->physical,info->surface,&caps)==VK_SUCCESS&&(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) modified.imageUsage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(smokeTest()&&(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) modified.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkResult result=data->CreateSwapchainKHR(device,&modified,allocator,swapchain);
    if(result==VK_SUCCESS) {
        auto swap=std::make_unique<Swapchain>(); swap->device=data; swap->handle=*swapchain; swap->extent=info->imageExtent; swap->format=info->imageFormat;
        swap->sharing=info->imageSharingMode;
        if(info->imageSharingMode==VK_SHARING_MODE_CONCURRENT&&info->pQueueFamilyIndices) swap->sharingFamilies.assign(info->pQueueFamilyIndices,info->pQueueFamilyIndices+info->queueFamilyIndexCount);
        uint32_t count=0; data->GetSwapchainImagesKHR(device,*swapchain,&count,nullptr); std::vector<VkImage> images(count); data->GetSwapchainImagesKHR(device,*swapchain,&count,images.data());
        swap->frames.resize(count); for(uint32_t i=0;i<count;++i) swap->frames[i].image=images[i]; swaps[*swapchain]=std::move(swap); note("Swapchain connected.");
    }
    return result;
}

EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device,VkSwapchainKHR swapchain,const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(guard); Device* data=findDevice(device); auto found=swaps.find(swapchain);
    if(found!=swaps.end()) { data->DeviceWaitIdle(device); cleanup(*found->second); swaps.erase(found); }
    data->DestroySwapchainKHR(device,swapchain,allocator);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* present) {
    std::lock_guard<std::recursive_mutex> lock(guard);
    auto foundQueue=queues.find(queue);
    auto foundDevice=devices.find(key(queue));
    if(foundDevice==devices.end()) return VK_ERROR_DEVICE_LOST;
    Device& data=*foundDevice->second;
    struct Boundary { DepthState& state; ~Boundary() { state.previousPresent=state.submitted; } } boundary{data.depth};
    updateViewerSettings();
    if(enabled&&foundQueue!=queues.end()&&present->swapchainCount==1) {
        auto found=swaps.find(present->pSwapchains[0]);
        uint32_t family=foundQueue->second.family;
        if(found!=swaps.end()&&family<data.families.size()) {
            Swapchain& swap=*found->second;
            uint32_t renderFamily=~0u; VkQueue renderQueue=overlayQueue(swap,queue,family,renderFamily);
            if(!renderQueue) { static ULONGLONG last=0; if(GetTickCount64()-last>5000) { note("Overlay withheld: no registered graphics queue can access this swapchain."); last=GetTickCount64(); } return data.QueuePresentKHR(queue,present); }
            try {
                if(!swap.ready) { initialize(swap,renderFamily); char message[200]; sprintf_s(message,"Overlay queues: graphics=%u present=%u sharing=%s.",renderFamily,family,swap.sharing==VK_SHARING_MODE_EXCLUSIVE?"exclusive":"concurrent"); note(message); }
                if(swap.family==renderFamily&&draw(swap,renderQueue,present->pImageIndices[0],*present,queue,family)) {
                    Frame& frame=swap.frames[present->pImageIndices[0]];
                    VkSemaphore* ready=family!=swap.family&&swap.sharing==VK_SHARING_MODE_EXCLUSIVE?&frame.presentReady:&frame.complete;
                    VkPresentInfoKHR modified=*present; modified.waitSemaphoreCount=1; modified.pWaitSemaphores=ready;
                    return data.QueuePresentKHR(queue,&modified);
                }
                if(swap.submitFailure!=VK_SUCCESS) return swap.submitFailure;
            } catch(const std::exception& error) { note(error.what()); enabled=false; note("Overlay disabled after error; game presentation is forwarded."); }
        }
    }
    return data.QueuePresentKHR(queue,present);
}

#include "depth_hooks.h"

static PFN_vkVoidFunction interception(const char* name) {
#define ENTRY(function) if(strcmp(name,#function)==0) return reinterpret_cast<PFN_vkVoidFunction>(function);
    ENTRY(vkGetInstanceProcAddr) ENTRY(vkGetDeviceProcAddr) ENTRY(vkCreateInstance) ENTRY(vkDestroyInstance) ENTRY(vkCreateDevice) ENTRY(vkDestroyDevice)
    ENTRY(vkGetDeviceQueue) ENTRY(vkGetDeviceQueue2) ENTRY(vkCreateSwapchainKHR) ENTRY(vkDestroySwapchainKHR) ENTRY(vkQueuePresentKHR)
    ENTRY(vkCreateImage) ENTRY(vkDestroyImage) ENTRY(vkCreateImageView) ENTRY(vkDestroyImageView)
    ENTRY(vkCreateRenderPass) ENTRY(vkCreateRenderPass2) ENTRY(vkCreateRenderPass2KHR) ENTRY(vkDestroyRenderPass) ENTRY(vkCreateFramebuffer) ENTRY(vkDestroyFramebuffer)
    ENTRY(vkBeginCommandBuffer) ENTRY(vkResetCommandBuffer) ENTRY(vkFreeCommandBuffers) ENTRY(vkCmdSetViewport)
    ENTRY(vkCmdBeginRenderPass) ENTRY(vkCmdEndRenderPass) ENTRY(vkCmdNextSubpass) ENTRY(vkCmdExecuteCommands)
    ENTRY(vkCmdBeginRenderPass2) ENTRY(vkCmdBeginRenderPass2KHR) ENTRY(vkCmdEndRenderPass2) ENTRY(vkCmdEndRenderPass2KHR) ENTRY(vkCmdNextSubpass2) ENTRY(vkCmdNextSubpass2KHR)
    ENTRY(vkCmdPipelineBarrier) ENTRY(vkCmdPipelineBarrier2) ENTRY(vkCmdPipelineBarrier2KHR) ENTRY(vkQueueSubmit) ENTRY(vkQueueSubmit2) ENTRY(vkQueueSubmit2KHR)
    ENTRY(vkCmdBeginRendering) ENTRY(vkCmdBeginRenderingKHR) ENTRY(vkCmdEndRendering) ENTRY(vkCmdEndRenderingKHR)
#undef ENTRY
    return nullptr;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char* name) {
    if(!name) return nullptr;
    if(!instance) return (strcmp(name,"vkGetInstanceProcAddr")==0||strcmp(name,"vkCreateInstance")==0)?interception(name):nullptr;
    std::lock_guard<std::recursive_mutex> lock(guard); auto found=instances.find(key(instance)); if(found==instances.end()) return nullptr;
    auto next=found->second->gipa(instance,name); if(!next) return nullptr;
    if(auto own=interception(name)) return own;
    return next;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name) {
    if(!name) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(guard); Device* data=findDevice(device);
    if(!data) return nullptr;
    auto next=data->gdpa(device,name);
    if(!next) return nullptr;
    if(auto own=interception(name)) return own;
    return next;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL DoomNegotiateLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if(!info||info->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if(info->loaderLayerInterfaceVersion>2) info->loaderLayerInterfaceVersion=2;
    info->pfnGetInstanceProcAddr=vkGetInstanceProcAddr; info->pfnGetDeviceProcAddr=vkGetDeviceProcAddr; info->pfnGetPhysicalDeviceProcAddr=physicalProcAddr; return VK_SUCCESS;
}

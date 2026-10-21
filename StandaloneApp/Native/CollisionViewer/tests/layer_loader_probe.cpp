#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"Usage: LayerLoaderProbe <layer-runtime-directory>\n";
        return 2;
    }

    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) {
        std::cerr << "Could not load the Windows Vulkan loader.\n";
        return 3;
    }

    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    auto createInstance = getInstanceProcAddr
        ? reinterpret_cast<PFN_vkCreateInstance>(getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"))
        : nullptr;
    if (!createInstance) {
        std::cerr << "The Vulkan loader did not expose vkCreateInstance.\n";
        return 4;
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "DEViL layer loader probe";
    application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &application;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = createInstance(&createInfo, nullptr, &instance);
    if (instance) {
        auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProcAddr(instance, "vkDestroyInstance"));
        if (destroyInstance) destroyInstance(instance, nullptr);
    }

    const std::filesystem::path logPath = std::filesystem::path(argv[1]) / L"VulkanViewer.log";
    std::ifstream logFile(logPath);
    std::string log((std::istreambuf_iterator<char>(logFile)), std::istreambuf_iterator<char>());
    if (log.find("Collision Vulkan layer loaded.") == std::string::npos) {
        std::cerr << "The Vulkan layer was not called. vkCreateInstance result=" << result << "\n";
        return 5;
    }

    std::cout << "PASS: Vulkan Loader called the DEViL layer. vkCreateInstance result=" << result << "\n";
    return 0;
}

#include "RasterCoreInternal.hpp"

#include <algorithm>

namespace RasterCore {

bool RasterPipeline::Impl::initialize(const Scene& sceneInput, InitOptions initOptions, std::string& errorMessage) {
    options = std::move(initOptions);
    shaderConfig = options.shaders;
    target = options.target;
    bufferConfig = options.buffer;
    windowConfig = options.window;
    scene = sceneInput;
    activeCamera = sceneInput.camera;
    modelTransform = cu::math::mat4::identity();

    if (target == OutputTarget::Window) {
        if (!windowConfig.window) {
            errorMessage = "RasterCore: window output requested but SDL_Window* is null";
            return false;
        }
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(windowConfig.window, &w, &h);
        if (w <= 0 || h <= 0) {
            errorMessage = "RasterCore: SDL window has invalid size";
            return false;
        }
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    } else {
        if (bufferConfig.width == 0 || bufferConfig.height == 0) {
            errorMessage = "RasterCore: buffer output requires non zero resolution";
            return false;
        }
        width = bufferConfig.width;
        height = bufferConfig.height;
        colorFormat = bufferConfig.colorFormat;
    }

    if (shaderConfig.searchPaths.empty()) {
        if (std::string(RASTER_CORE_SHADER_DIR).size() > 0) {
            shaderConfig.searchPaths.push_back(std::filesystem::path(RASTER_CORE_SHADER_DIR));
        }
        if (std::string(RASTER_CORE_SHADER_FALLBACK_DIR).size() > 0) {
            shaderConfig.searchPaths.push_back(std::filesystem::path(RASTER_CORE_SHADER_FALLBACK_DIR));
        }
        shaderConfig.searchPaths.push_back(std::filesystem::path("shaders"));
    }

    if (!createInstanceIfNeeded(errorMessage)) {
        return false;
    }
    if (!createGpuIfNeeded(errorMessage)) {
        return false;
    }

    return rebuildPipeline(errorMessage);
}

bool RasterPipeline::Impl::createInstanceIfNeeded(std::string& errorMessage) {
    if (instance) {
        return true;
    }

    renderApi::instance::Config instanceConfig;

    if (options.enableValidation) {
        instanceConfig = renderApi::instance::Config::DebugDefault("RasterCore");
    } else {
        instanceConfig = renderApi::instance::Config::ReleaseDefault("RasterCore");
    }

    std::vector<const char*> extensions;

    unsigned int count = 0;
    if (target == OutputTarget::Window) {
        if (!SDL_Vulkan_GetInstanceExtensions(windowConfig.window, &count, nullptr)) {
            errorMessage = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
            return false;
        }

        std::vector<const char*> windowExtensions(count);
        if (!SDL_Vulkan_GetInstanceExtensions(windowConfig.window, &count, windowExtensions.data())) {
            errorMessage = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            extensions.push_back(windowExtensions[i]);
        }
    }

    for (const auto& ext : options.instanceExtensions) {
        extensions.push_back(internal::pinCString(ext));
    }

    instanceConfig.extensions = extensions;

    for (const auto& layer : options.instanceLayers) {
        instanceConfig.layers.push_back(internal::pinCString(layer));
    }

    instanceName = "RasterCoreInstance_" + std::to_string(reinterpret_cast<uintptr_t>(this));
    instanceConfig.instanceName = instanceName;

    try {
        instance = std::make_unique<renderApi::instance::RenderInstance>(instanceConfig);
    } catch (const std::exception& e) {
        errorMessage = std::string("RasterCore: instance creation failed: ") + e.what();
        return false;
    }

    return true;
}

bool RasterPipeline::Impl::createGpuIfNeeded(std::string& errorMessage) {
    if (gpu) {
        return true;
    }

    if (!instance) {
        errorMessage = "RasterCore: cannot create GPU without instance";
        return false;
    }

    renderApi::device::Config gpuConfig;
    gpuConfig.graphics = 1;
    gpuConfig.compute = 0;
    gpuConfig.transfer = 0;

    if (!options.gpuName.empty()) {
        gpuConfig.physicalDevice = VK_NULL_HANDLE;
    }

    auto initResult = instance->addGPU(gpuConfig);
    if (initResult != renderApi::device::InitDeviceResult::INIT_DEVICE_SUCCESS) {
        errorMessage = "RasterCore: device init failed";
        return false;
    }

    gpu = instance->getGPU(0);
    if (!gpu) {
        errorMessage = "RasterCore: failed to retrieve GPU after initialization";
        return false;
    }

    return true;
}

} // namespace RasterCore

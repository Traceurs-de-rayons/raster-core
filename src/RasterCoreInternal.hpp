#ifndef RASTER_CORE_INTERNAL_HPP
#define RASTER_CORE_INTERNAL_HPP

#include "RasterCore.hpp"

#include "buffer/buffer.hpp"
#include "descriptor/descriptorSetManager.hpp"
#include "gpuTask/gpuTask.hpp"
#include "image/image.hpp"
#include "pipeline/graphicsPipeline.hpp"
#include "renderApi.hpp"
#include "renderDevice.hpp"
#include "renderInstance.hpp"

#include <SDL2/SDL_vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RasterCore::internal {

const char* pinCString(const std::string& value);
std::filesystem::path resolveShaderPath(const ShaderConfig& cfg, const std::string& filename);
std::vector<uint32_t> readSpirvFile(const std::filesystem::path& path);
std::array<float, 16> toFloatArray(const cu::math::mat4& m);

struct alignas(16) CameraUniform {
    std::array<float, 16> model;
    std::array<float, 16> view;
    std::array<float, 16> proj;
};

struct GpuVertex {
    float position[3];
    float color[3];
    float uv[2];
    uint32_t textureId;
};

} // namespace RasterCore::internal

namespace RasterCore {

struct RasterPipeline::Impl {
    Scene scene;
    InitOptions options;
    OutputTarget target = OutputTarget::Buffer;
    BufferOutputConfig bufferConfig{};
    WindowOutputConfig windowConfig{};
    ShaderConfig shaderConfig{};
    cu::math::mat4 modelTransform = cu::math::mat4::identity();

    std::string instanceName;
    std::unique_ptr<renderApi::instance::RenderInstance> instance;
    renderApi::device::GPU* gpu = nullptr;

    std::unique_ptr<renderApi::gpuTask::GpuTask> task;
    renderApi::gpuTask::GraphicsPipeline* pipeline = nullptr;

    renderApi::Buffer vertexBuffer;
    renderApi::Buffer indexBuffer;
    renderApi::Buffer deviceColorBuffer;
    renderApi::Buffer cameraUniformBuffer;

    std::vector<renderApi::Texture> gpuTextures;
    Camera activeCamera;

    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    bool initialize(const Scene& sceneInput, InitOptions initOptions, std::string& errorMessage);
    bool rebuildPipeline(std::string& errorMessage);
    bool createInstanceIfNeeded(std::string& errorMessage);
    bool createGpuIfNeeded(std::string& errorMessage);
    bool configurePipelineShaders(std::string& errorMessage);
    bool uploadSceneGeometry(std::string& errorMessage);
    bool uploadTextures(std::string& errorMessage);
    bool ensureDeviceBuffer(std::string& errorMessage);
    bool ensureCameraUniformBuffer(std::string& errorMessage);
    bool setupDescriptorSets(std::string& errorMessage);
    void updateCameraUniform();
    void copyColorImageToDeviceBuffer();
    void transitionColorImageForShaderRead();
    void destroyGpuTask();
};

} // namespace RasterCore

#endif // RASTER_CORE_INTERNAL_HPP

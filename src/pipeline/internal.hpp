#ifndef RASTER_CORE_INTERNAL_HPP
# define RASTER_CORE_INTERNAL_HPP

# include "rasterCore.hpp"

# include "buffer.hpp"
# include "descriptorSetManager.hpp"
# include "gpuTask.hpp"
# include "image.hpp"
# include "graphicsPipeline.hpp"
# include "renderDevice.hpp"

# include <SDL2/SDL_vulkan.h>
# include <array>
# include <cstdint>
# include <filesystem>
# include <memory>
# include <string>
# include <vector>

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
	uint32_t modelMatrixId;
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

	renderApi::device::GPU* gpu = nullptr;

	std::unique_ptr<renderApi::gpuTask::GpuTask> task;
	renderApi::gpuTask::GraphicsPipeline* pipeline = nullptr;

	renderApi::Buffer deviceColorBuffer;
	renderApi::Buffer cameraUniformBuffer;
	Camera activeCamera;

	uint32_t width = 0;
	uint32_t height = 0;
	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

	bool initialize(const Scene& sceneInput, InitOptions initOptions, std::string& errorMessage);
	bool rebuildPipeline(std::string& errorMessage);
	bool updateSceneData(std::string& errorMessage);
	bool configurePipelineShaders(std::string& errorMessage);
	bool ensureDeviceBuffer(std::string& errorMessage);
	bool ensureCameraUniformBuffer(std::string& errorMessage);
	bool setupDescriptorSetsWithSharedResources(std::string& errorMessage);
	void updateCameraUniform();
	void copyColorImageToDeviceBuffer();
	void transitionColorImageForShaderRead();
	void destroyGpuTask();
};

} // namespace RasterCore

#endif // RASTER_CORE_INTERNAL_HPP
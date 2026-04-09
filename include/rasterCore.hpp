#pragma once

#include "Vertex.hpp"
#include "rasterTypes.hpp"
#include "core-utils.hpp"
#include "viewportManager.hpp"

#include <SDL2/SDL.h>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace renderApi {
class Buffer;
class Texture;
namespace device {
struct GPU;
} // namespace device
namespace gpuTask {
class GpuTask;
class GraphicsPipeline;
} // namespace gpuTask
} // namespace renderApi

namespace RasterCore {

// Shared GPU resources used by all viewports
struct SharedGpuResources {
	renderApi::device::GPU* gpu = nullptr;
	renderApi::Buffer* vertexBuffer = nullptr;
	renderApi::Buffer* indexBuffer = nullptr;
	renderApi::Buffer* modelMatrixBuffer = nullptr;
	std::vector<renderApi::Texture>* textures = nullptr;
	uint32_t indexCount = 0;
	uint32_t vertexCount = 0;
};

struct Triangle {
	Vertex vertices[3];
};

struct TextureData {
	std::string name;
	int width = 0;
	int height = 0;
	int channels = 0;
	std::vector<unsigned char> pixels;

	bool isValid() const { return !pixels.empty() && width > 0 && height > 0; }
};

struct BufferOutputConfig {
	uint32_t width = 1280;
	uint32_t height = 720;
	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
};

struct WindowOutputConfig {
	SDL_Window* window = nullptr;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	uint32_t swapchainImageCount = 2;
	bool enableVsync = true;
};

#ifndef RASTER_CORE_SHADER_DIR
#define RASTER_CORE_SHADER_DIR ""
#endif

#ifndef RASTER_CORE_SHADER_FALLBACK_DIR
#define RASTER_CORE_SHADER_FALLBACK_DIR ""
#endif

struct ShaderConfig {
	std::vector<std::filesystem::path> searchPaths = {
			std::filesystem::path(RASTER_CORE_SHADER_DIR),
			std::filesystem::path(RASTER_CORE_SHADER_FALLBACK_DIR),
			std::filesystem::path("shaders")};
	std::string mainVertex = "textured.vert.spv";
	std::string mainFragment = "textured.frag.spv";
	std::string debugVertex = "debug.vert.spv";
	std::string debugFragment = "debug.frag.spv";
};

struct InitOptions {
	OutputTarget target = OutputTarget::Buffer;
	BufferOutputConfig buffer{};
	WindowOutputConfig window{};
	ShaderConfig shaders{};
	bool enableValidation = false;
	bool preferMeshShaders = false;
	std::vector<std::string> instanceExtensions{};
	std::vector<std::string> instanceLayers{};
	std::string gpuName{};
	SharedGpuResources* sharedResources = nullptr;
};

class RasterPipeline;

struct InitResult {
	bool success = false;
	std::string errorMessage;
	std::shared_ptr<RasterPipeline> pipeline;
};

InitResult initRasterisation(const InitOptions& options = InitOptions());

class RasterPipeline {
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		ViewportManager* viewportManager_;

		explicit RasterPipeline(std::unique_ptr<Impl> impl);

		friend InitResult initRasterisation(const InitOptions&);

	public:
		~RasterPipeline();
		RasterPipeline(RasterPipeline&&) noexcept;
		RasterPipeline& operator=(RasterPipeline&&) noexcept;

		RasterPipeline(const RasterPipeline&) = delete;
		RasterPipeline& operator=(const RasterPipeline&) = delete;

		void drawFrame();
		void waitIdle();
		void pauseGpuTask();
		void resumeGpuTask();

		void setCamera(const Camera& camera);
		Camera getCamera() const;
		void setModelTransform(const cu::math::mat4& transform);
		cu::math::mat4 getModelTransform() const;

		void setViewportManager(ViewportManager* viewportManager);
		ViewportManager* getViewportManager();

		OutputTarget target() const;
		uint32_t width() const;
		uint32_t height() const;

		renderApi::Buffer* deviceBuffer();
		renderApi::gpuTask::GpuTask* gpuTask();
		renderApi::device::GPU* gpu();
		VkRenderPass getRenderPass();

		void refreshSharedResources();

		void* getColorImage() const;
		void* getColorImageView() const;

		void resize();

		// Second pipeline control
		void setEnableSecondPipeline(bool enable);
		bool isSecondPipelineEnabled() const;
		renderApi::gpuTask::GraphicsPipeline* getSecondPipeline() const;
};

}

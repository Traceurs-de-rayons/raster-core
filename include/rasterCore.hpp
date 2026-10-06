#pragma once

#include "Vertex.hpp"
#include "core-utils.hpp"
#include "rasterTypes.hpp"
#include "renderGraph.hpp"

#include "buffer.hpp"
#include "image.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace renderApi::device {
struct GPU;
}

#ifndef RASTER_CORE_SHADER_DIR
#define RASTER_CORE_SHADER_DIR ""
#endif

#ifndef RASTER_CORE_SHADER_FALLBACK_DIR
#define RASTER_CORE_SHADER_FALLBACK_DIR ""
#endif

namespace RasterCore {

enum class LightType {
	Directional, // sun or directional light: only `direction` matters
	Point,		 // omni light at `position`, attenuated with distance
	Spot,		 // cone from `position` along `direction`, attenuated with distance
};

struct Light {
	LightType type = LightType::Directional;
	cu::math::vec3 position{0.0f};
	cu::math::vec3 direction{0.0f, -1.0f, 0.0f}; // direction the light travels
	cu::math::vec3 color{1.0f};
	float intensity = 1.0f;
	float angularDiameter = 0.0f; // directional only, degrees: shadow penumbra size (0 = sharp)
	float coneAngle = 45.0f;	  // spot only, degrees: full angle of the cone
	float coneBlend = 0.15f;	  // spot only: fraction of the cone faded out at its edge (0 = hard edge)
	// Directional: the first one with this flag gets cascaded shadows.
	// Point / spot: the kMaxPointShadows most relevant ones (brightness at the camera) get cube shadows.
	bool castsShadow = false;
};

inline constexpr uint32_t kMaxLights = 64;

// Scene data uploaded once by the scene manager and shared by every viewport.
struct SharedGpuResources {
	renderApi::device::GPU* gpu = nullptr;
	renderApi::Buffer vertexBuffer;
	renderApi::Buffer indexBuffer;
	renderApi::Buffer modelMatrixBuffer;
	renderApi::Buffer materialBuffer;
	renderApi::Buffer perDrawBuffer;
	renderApi::Buffer drawCommandsBuffer;
	std::vector<renderApi::Texture> textures;
	uint32_t indexCount = 0;
	uint32_t vertexCount = 0;
	uint32_t drawCount = 0;
	// Bumped by the owner whenever the geometry or a model matrix changes: cached shadow maps
	// are only valid for the version they were rendered with.
	uint64_t geometryVersion = 0;

	bool isLoaded() const { return gpu != nullptr && vertexBuffer.isValid(); }

	void destroy() {
		vertexBuffer.destroy();
		indexBuffer.destroy();
		modelMatrixBuffer.destroy();
		materialBuffer.destroy();
		perDrawBuffer.destroy();
		drawCommandsBuffer.destroy();
		for (auto& texture : textures)
			texture.destroy();
		textures.clear();
		indexCount = 0;
		vertexCount = 0;
		drawCount = 0;
	}
};

struct ShadowSettings {
	float distance = 80.0f;		   // sun shadows cover the camera frustum up to this depth, then fade out
	float cascadeSplitLambda = 0.8f; // cascade split blend: 0 = uniform, 1 = logarithmic
	float cascadeBlend = 0.1f;	   // fraction of each cascade cross-faded with the next one
	float maxPointShadowRange = 100.0f; // point light shadow maps stop at this distance
};

struct InitOptions {
	uint32_t width = 1280;
	uint32_t height = 720;
	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
	RenderGraphConfig graph = RenderGraphConfig::deferred();
	ShadowSettings shadows{};
	std::vector<std::filesystem::path> shaderSearchPaths = {
			std::filesystem::path(RASTER_CORE_SHADER_DIR),
			std::filesystem::path(RASTER_CORE_SHADER_FALLBACK_DIR),
			std::filesystem::path("shaders")};
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
	public:
		struct Impl;

		~RasterPipeline();
		RasterPipeline(RasterPipeline&&) noexcept;
		RasterPipeline& operator=(RasterPipeline&&) noexcept;

		RasterPipeline(const RasterPipeline&) = delete;
		RasterPipeline& operator=(const RasterPipeline&) = delete;

		// Uploads per-frame uniforms; the GPU thread renders the passes on its own.
		void drawFrame();
		bool resize(uint32_t width, uint32_t height, std::string* errorMessage = nullptr);

		void setCamera(const Camera& camera);
		Camera getCamera() const;
		// At most kMaxLights are used; extra lights are ignored.
		void setLights(const std::vector<Light>& lights);
		const std::vector<Light>& getLights() const;

		uint32_t width() const;
		uint32_t height() const;
		renderApi::device::GPU* gpu();

		// Final image of the graph (VK_IMAGE_LAYOUT_GENERAL once a frame completes).
		VkImage getColorImage() const;
		VkImageView getColorImageView() const;

	private:
		std::unique_ptr<Impl> impl_;

		explicit RasterPipeline(std::unique_ptr<Impl> impl);
		friend InitResult initRasterisation(const InitOptions&);
};

}

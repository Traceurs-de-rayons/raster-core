#pragma once

#include "Vertex.hpp"
#include "core-utils.hpp"
#include "rasterTypes.hpp"
#include "renderGraph.hpp"

#include "buffer.hpp"
#include "image.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
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
	renderApi::Buffer skinBuffer; // second vertex stream: 4 bone indices and 4 weights, one byte each
	renderApi::Buffer indexBuffer;
	renderApi::Buffer modelMatrixBuffer;
	renderApi::Buffer materialBuffer;
	renderApi::Buffer perDrawBuffer;
	// The indirect draw list (one command per draw; a hidden draw has an instance count of 0).
	// The GPU reads it while it draws a frame, so it is never written in place: a change goes
	// into the next buffer of this ring, and the frames recorded from then on use that one
	// (setDrawCommands()). Rewriting the buffer a frame in flight is reading hangs the GPU:
	// seen on an AMD card, as a device lost a few seconds after things started to show and hide.
	static constexpr size_t kDrawCommandBuffers = 4; // more than the frames that can be in flight
	std::array<renderApi::Buffer, kDrawCommandBuffers> drawCommands;
	uint32_t drawCommandsCurrent = 0;
	renderApi::Buffer& drawCommandsBuffer() { return drawCommands[drawCommandsCurrent]; }
	const renderApi::Buffer& drawCommandsBuffer() const { return drawCommands[drawCommandsCurrent]; }
	// Makes `size` bytes of draw commands the list drawn from now on. `gpu` must be set.
	bool setDrawCommands(const void* data, size_t size) {
		const uint32_t next = (drawCommandsCurrent + 1) % kDrawCommandBuffers;
		if (!drawCommands[next].isValid() || !drawCommands[next].upload(data, size))
			return false;
		drawCommandsCurrent = next;
		return true;
	}
	// Creates the ring, every buffer holding the same list, with room for `capacity` bytes (at
	// least `size`): a longer list later only goes through setDrawCommands().
	bool createDrawCommands(const void* data, size_t size, size_t capacity = 0) {
		capacity = std::max(capacity, size);
		for (renderApi::Buffer& buffer : drawCommands) {
			buffer.destroy();
			buffer.create(gpu, capacity > 0 ? capacity : 1, renderApi::BufferType::INDIRECT, renderApi::BufferUsage::DYNAMIC);
			if (!buffer.isValid() || (size > 0 && !buffer.upload(data, size)))
				return false;
		}
		drawCommandsCurrent = 0;
		return true;
	}
	renderApi::Buffer boneBuffer;	  // bone matrices of every skinned draw, one after the other
	renderApi::Buffer drawInfoBuffer; // per draw: index of its first bone, bit 31 set for a view model
	std::vector<renderApi::Texture> textures;
	uint32_t indexCount = 0;
	uint32_t vertexCount = 0;
	uint32_t drawCount = 0;
	// Bumped by the owner whenever the geometry or a model matrix changes: cached shadow maps
	// are only valid for the version they were rendered with.
	uint64_t geometryVersion = 0;

	// Bit of a drawInfoBuffer entry marking a view model (see Asset::setViewModel()).
	static constexpr uint32_t kViewModelBit = 1u << 31;

	bool isLoaded() const { return gpu != nullptr && vertexBuffer.isValid(); }

	void destroy() {
		vertexBuffer.destroy();
		skinBuffer.destroy();
		boneBuffer.destroy();
		drawInfoBuffer.destroy();
		indexBuffer.destroy();
		modelMatrixBuffer.destroy();
		materialBuffer.destroy();
		perDrawBuffer.destroy();
		for (renderApi::Buffer& buffer : drawCommands)
			buffer.destroy();
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

// Look of the toon graph (RenderGraphConfig::toon()); may change every frame.
struct ToonParams {
	float speedEffect = 0.0f;	   // 0..1: radial blur and vignette, driven by the player speed
	float time = 0.0f;			   // seconds
	float outlineStrength = 0.85f; // 0 = no outline
	float bloomStrength = 0.9f;
	cu::math::vec3 fogColor{0.8f, 0.85f, 0.9f};
	float fogDensity = 0.006f;	   // per world unit
	float fogHeight = 2.0f;		   // the fog thins out above this height
	float gridStrength = 1.0f;	   // world-space grid on the grey surfaces
	float bands = 1.0f;			   // steps of the shadow penumbra (1 = one clean edge)
	float emissiveBrightness = 2.2f; // above 1.1 an emissive surface starts to bloom
	cu::math::vec3 outlineColor{0.06f, 0.07f, 0.1f};
	float ambient = 0.35f;
	// The sight the player aims through, drawn by the last pass: where its lens is on screen
	// (0..1 from the top left corner), its radius as a fraction of the screen height, and how
	// much of the effect shows (0 = none). A positive radius is a scope: a reticle, the rim of
	// the lens, everything around it dark and blurred (the picture a scope shows is rendered
	// with it, filling the image). A negative one is the window of a red dot sight: the dot is
	// drawn in the middle of the screen when that window is over it.
	float sightX = 0.5f;
	float sightY = 0.5f;
	float sightRadius = 0.0f;
	float sightStrength = 0.0f;
	// The grade of the picture, in the last pass. Neutral at these defaults.
	float vignette = 1.0f;			  // the darkening of the corners (1 = as made)
	float chromaticAberration = 0.0f; // 0..1: red and blue pulled apart toward the edges
	float filmGrain = 0.0f;			  // 0..1
	float sharpen = 0.0f;			  // 0..1
	float saturation = 1.0f;		  // 0 = grey
	float contrast = 1.0f;
	float exposure = 1.0f;
	cu::math::vec3 colorFilter{1.0f, 1.0f, 1.0f}; // multiplies the picture
	// What happens to the player: 0..1.
	float lowHealth = 0.0f; // the picture loses its color and the edges pulse red
	float damage = 0.0f;	// a red flash at the edges, just hit
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
		// The buffers or textures of SharedGpuResources were replaced (a scene that grew): the next
		// frames read the new ones. Only while no frame is recorded or executing (the caller holds
		// GPU::GpuTasksMutex); nothing else is rebuilt.
		void refreshSceneBindings();

		void setCamera(const Camera& camera);
		Camera getCamera() const;
		// At most kMaxLights are used; extra lights are ignored.
		void setLights(const std::vector<Light>& lights);
		// Color of the pixels no geometry covers.
		void setBackgroundColor(const cu::math::vec3& color);
		// 8 float4 handed to every shader as LightUBO::effects (shaders/frame_uniforms.slang):
		// what they mean is up to the graph being rendered.
		void setEffectParams(const std::array<std::array<float, 4>, 8>& params);
		// Parameters of RenderGraphConfig::toon().
		void setToonParams(const ToonParams& params);
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

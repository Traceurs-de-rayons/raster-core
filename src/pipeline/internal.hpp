#pragma once

#include "rasterCore.hpp"

#include "RenderGraph.hpp"
#include "ShadowLayout.hpp"
#include "buffer.hpp"
#include "image.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// GPU-side layouts. Buffers read by the shaders only use float4/uint4 members (and raw float4
// arrays for storage buffers: radv crashes compiling Slang's StructuredBuffer<struct>).

namespace RasterCore::internal {

// Must match CameraUBO in the shaders.
struct alignas(16) CameraUniform {
	std::array<float, 16> viewModelProj; // projection of the view models
	std::array<float, 16> view;
	std::array<float, 16> proj;
};

// Must match LightUBO in the shaders.
struct alignas(16) LightUniform {
	std::array<float, 4>	cameraPosition; // xyz
	std::array<float, 4>	cameraForward;	// xyz, normalized
	std::array<float, 4>	cascadeSplits;	// view depth at which each cascade ends
	std::array<float, 4>	shadowParams;	// x = tan(sun half angle), y = cascade blend fraction
	std::array<uint32_t, 4> counts;			// x = lights, y = active cascades (0 = no sun shadow)
	std::array<std::array<float, 4>, 4> effects; // free parameters for the graph's shaders
};

// Must match loadLight() in lighting.frag.slang: 4 float4 per light.
struct alignas(16) GpuLight {
	std::array<float, 4> positionType;		 // xyz = position, w = 0 directional / 1 point / 2 spot
	std::array<float, 4> directionIntensity; // xyz = travel direction, w = intensity
	std::array<float, 4> colorShadow;		 // rgb = color, w = first shadow view index or -1
	std::array<float, 4> cone;				 // spot: x = cos(outer half angle), y = cos(inner half angle)
};

// Must match loadShadowView() in the shaders: 10 float4 per view.
struct alignas(16) GpuShadowView {
	std::array<float, 16> view;		 // column-major (cu::math layout)
	std::array<float, 16> proj;
	std::array<float, 4>  atlasRect; // xy = tile offset, zw = tile size, in atlas UV
	std::array<float, 4>  params;	 // cascades: x = texel world size, y = depth range, z = tile UV per world unit
									 // point faces: x = texel size per unit of distance; all: w = 1 / tile resolution
};

// Everything uploaded for one frame of lighting.
struct LightFrame {
	LightUniform			   uniform{};
	std::vector<GpuLight>	   lights;
	std::vector<GpuShadowView> shadowViews; // kMaxShadowViews entries
	// One bit per atlas tile (see RenderGraph::setViews()).
	uint64_t				   cascadeTiles		= 0; // sun cascades in use
	uint64_t				   pointTiles		= 0; // cube faces in use
	uint64_t				   pointVisibleTiles = 0; // cube faces of lights that can reach the camera frustum
	uint64_t				   geometryVersion	= 0; // SharedGpuResources::geometryVersion the frame was built for
};

// Which light owns each point shadow slot. Kept between frames so a light stays in its slot (and
// keeps its cached shadow map) while the camera moves; -1 = free.
using ShadowSlots = std::array<int, shadow::kMaxPointShadows>;

void writeCameraUniform(CameraUniform& out, const Camera& camera, uint32_t width, uint32_t height);
void buildLightFrame(LightFrame& out, const std::vector<Light>& lights, const Camera& camera, float aspect, const ShadowSettings& settings,
					 ShadowSlots& slots);

// Shadows.cpp
void writeCascades(LightFrame& frame, const cu::math::vec3& lightDirection, float sunAngularDiameter, const Camera& camera, float aspect,
				   const ShadowSettings& settings);
void writePointShadow(LightFrame& frame, uint32_t slot, const cu::math::vec3& position, float intensity, float maxRange);
// Distance after which a point light no longer lights anything visibly (also its shadow map range).
float pointLightRange(float intensity, float maxRange);

}

namespace RasterCore {

struct RasterPipeline::Impl {
	InitOptions options;
	renderApi::device::GPU* gpu = nullptr;

	Camera camera;
	std::vector<Light> lights;
	std::array<std::array<float, 4>, 4> effects{};

	renderApi::Buffer cameraBuffer;
	renderApi::Buffer lightBuffer;
	renderApi::Buffer lightListBuffer;
	renderApi::Buffer shadowViewBuffer;
	renderApi::Sampler linearSampler;
	renderApi::Sampler nearestSampler;
	VkSampler shadowSampler = VK_NULL_HANDLE; // depth comparison sampler
	internal::RenderGraph graph;

	// Latest uniforms built by the main thread, copied to the buffers by the GPU thread
	// between two frames (see GraphContext::beforeRecord).
	std::mutex pendingMutex;
	internal::CameraUniform pendingCamera{};
	internal::LightFrame pendingFrame;
	bool pendingDirty = false;

	internal::ShadowSlots shadowSlots; // main thread

	// What each shadow atlas tile currently contains (GPU thread): a tile is only drawn again
	// when its view or the scene geometry changed.
	std::array<internal::GpuShadowView, shadow::kMaxShadowViews> renderedViews{};
	std::array<uint64_t, shadow::kMaxShadowViews> renderedGeometry{};
	std::array<bool, shadow::kMaxShadowViews> renderedValid{};

	~Impl();

	bool initialize(const InitOptions& initOptions, std::string& error);
	bool createSamplers(std::string& error);
	bool buildGraph(std::string& error);
	void uploadUniforms();
	void flushUniforms();
	uint64_t dirtyTiles(const internal::LightFrame& frame, uint32_t viewBase, uint64_t tiles);
};

}

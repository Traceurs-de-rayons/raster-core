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
	std::array<float, 16> model;
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
	uint32_t				   cascadeViews = 0;
	uint32_t				   pointViews	= 0;
};

void writeCameraUniform(CameraUniform& out, const Camera& camera, uint32_t width, uint32_t height);
void buildLightFrame(LightFrame& out, const std::vector<Light>& lights, const Camera& camera, float aspect, const ShadowSettings& settings);

// Shadows.cpp
void writeCascades(LightFrame& frame, const cu::math::vec3& lightDirection, float sunAngularDiameter, const Camera& camera, float aspect,
				   const ShadowSettings& settings);
void writePointShadow(LightFrame& frame, uint32_t slot, const cu::math::vec3& position, float intensity, float maxRange);

}

namespace RasterCore {

struct RasterPipeline::Impl {
	InitOptions options;
	renderApi::device::GPU* gpu = nullptr;

	Camera camera;
	std::vector<Light> lights;

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

	~Impl();

	bool initialize(const InitOptions& initOptions, std::string& error);
	bool createSamplers(std::string& error);
	bool buildGraph(std::string& error);
	void uploadUniforms();
	void flushUniforms();
};

}

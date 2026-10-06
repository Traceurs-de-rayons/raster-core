#pragma once

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Declarative description of the passes rendered by a RasterPipeline.
//
// Every pass writes named images ("resources") and may sample the resources written by
// earlier passes. The pipeline allocates the images, inserts the layout barriers and binds
// everything, so adding a pass is just describing it:
//
//     auto graph = RasterCore::RenderGraphConfig::deferred();
//     graph.addPostEffect("greyscale", "post_greyscale.frag.spv");
//
// Passes run in the order they are added.

namespace RasterCore {

// Descriptor bindings reserved by the pipeline (set 0 of every pass). Shaders must use
// these numbers; pass inputs may use any other binding.
namespace binding {
	inline constexpr uint32_t SceneTextures = 0;  // Scene passes only: Sampler2D[]
	inline constexpr uint32_t Camera		= 16; // all passes: CameraUBO
	inline constexpr uint32_t ModelMatrices = 17; // Scene passes only: StructuredBuffer<float4x4>
	inline constexpr uint32_t PerDraw		= 18; // Scene passes only: StructuredBuffer<uint> (material id)
	inline constexpr uint32_t Materials		= 19; // Scene passes only: StructuredBuffer<uint> (texture index)
	inline constexpr uint32_t Light			= 21; // all passes: LightUBO (count + shadow caster)
	inline constexpr uint32_t Lights		= 22; // all passes: StructuredBuffer<float4> (3 per light)
	inline constexpr uint32_t ShadowViews	= 23; // all passes: StructuredBuffer<float4> (10 per shadow view)
} // namespace binding

// Push constants of passes rendering several views (see PassDesc::views).
struct ViewPushConstants {
	uint32_t viewIndex; // PassDesc::viewBase + index of the view being rendered
};

enum class SamplerKind {
	Linear,
	Nearest,
	ShadowCompare, // depth comparison (LESS_OR_EQUAL) with linear filtering: hardware PCF
};

enum class PassType {
	Scene,		// rasterizes the scene geometry (shared vertex/index/indirect buffers)
	Fullscreen, // draws one fullscreen triangle (post.vert by default)
};

struct PassOutput {
	std::string			 name;
	VkFormat			 format = VK_FORMAT_UNDEFINED; // UNDEFINED = pipeline color format
	std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

struct PassInput {
	uint32_t	binding = 0;
	std::string resource; // name of a color or depth output of an earlier pass
	SamplerKind sampler = SamplerKind::Linear;
};

struct PassDesc {
	std::string				name;
	PassType				type = PassType::Fullscreen;
	std::string				vertexShader;	// empty = default for the pass type
	std::string				fragmentShader; // may be empty for depth-only scene passes
	std::vector<PassOutput> colorOutputs;
	std::string				depthOutput; // non-empty = expose the depth attachment under this name
	std::vector<PassInput>	inputs;
	bool					depthTest = false;
	VkCullModeFlags			cullMode  = VK_CULL_MODE_NONE;
	uint32_t				width  = 0; // 0 = viewport size
	uint32_t				height = 0;

	// Multi-view: the pass extent is split into a viewColumns x viewRows grid and the scene is
	// drawn once per tile, with ViewPushConstants{viewBase + tile index} pushed to the vertex
	// shader. Tiles are filled row by row. Such a pass is depth only and keeps its content between
	// frames: only the tiles given to RenderGraph::setViews() are drawn again (at most 64 views).
	uint32_t viewColumns = 0; // 0 = single view
	uint32_t viewRows	 = 0;
	uint32_t viewBase	 = 0;

	float depthBiasConstant = 0.0f;
	float depthBiasSlope	= 0.0f;

	static PassDesc scene(std::string name, std::string vertexShader, std::string fragmentShader);
	static PassDesc fullscreen(std::string name, std::string fragmentShader);

	PassDesc& write(std::string resource, VkFormat format = VK_FORMAT_UNDEFINED);
	PassDesc& writeDepth(std::string resource);
	PassDesc& read(uint32_t binding, std::string resource, SamplerKind sampler = SamplerKind::Linear);
	PassDesc& extent(uint32_t w, uint32_t h);
	PassDesc& views(uint32_t columns, uint32_t rows, uint32_t base = 0);
	PassDesc& depthBias(float constant, float slope);
	uint32_t  viewCount() const { return viewColumns * viewRows; }
	// Clear color of the most recently written color output
	PassDesc& clear(float r, float g, float b, float a = 1.0f);
};

struct RenderGraphConfig {
	std::string			  defaultSceneVertex	  = "geometry.vert.spv";
	std::string			  defaultFullscreenVertex = "post.vert.spv";
	std::vector<PassDesc> passes; // executed in order
	std::string			  output; // resource displayed by the viewport

	RenderGraphConfig& addPass(PassDesc pass);
	// Fullscreen pass reading the current output at binding 0 and becoming the new output.
	RenderGraphConfig& addPostEffect(const std::string& name, const std::string& fragmentShader);

	// shadow -> G-buffer -> lighting -> transparency
	static RenderGraphConfig deferred();
};

} // namespace RasterCore

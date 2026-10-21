#include "renderGraph.hpp"

#include "ShadowLayout.hpp"

#include <utility>

namespace RasterCore {

PassDesc PassDesc::scene(std::string name, std::string vertexShader, std::string fragmentShader) {
	PassDesc pass;
	pass.name			= std::move(name);
	pass.type			= PassType::Scene;
	pass.vertexShader	= std::move(vertexShader);
	pass.fragmentShader = std::move(fragmentShader);
	pass.depthTest		= true;
	return pass;
}

PassDesc PassDesc::fullscreen(std::string name, std::string fragmentShader) {
	PassDesc pass;
	pass.name			= std::move(name);
	pass.type			= PassType::Fullscreen;
	pass.fragmentShader = std::move(fragmentShader);
	return pass;
}

PassDesc& PassDesc::write(std::string resource, VkFormat format) {
	colorOutputs.push_back({std::move(resource), format});
	return *this;
}

PassDesc& PassDesc::writeDepth(std::string resource) {
	depthOutput = std::move(resource);
	return *this;
}

PassDesc& PassDesc::read(uint32_t bindingIndex, std::string resource, SamplerKind sampler) {
	inputs.push_back({bindingIndex, std::move(resource), sampler});
	return *this;
}

PassDesc& PassDesc::views(uint32_t columns, uint32_t rows, uint32_t base) {
	viewColumns = columns;
	viewRows	= rows;
	viewBase	= base;
	return *this;
}

PassDesc& PassDesc::depthBias(float constant, float slope) {
	depthBiasConstant = constant;
	depthBiasSlope	  = slope;
	return *this;
}

PassDesc& PassDesc::extent(uint32_t w, uint32_t h) {
	width  = w;
	height = h;
	return *this;
}

PassDesc& PassDesc::clear(float r, float g, float b, float a) {
	if (!colorOutputs.empty())
		colorOutputs.back().clearColor = {r, g, b, a};
	return *this;
}

RenderGraphConfig& RenderGraphConfig::addPass(PassDesc pass) {
	passes.push_back(std::move(pass));
	return *this;
}

RenderGraphConfig& RenderGraphConfig::addPostEffect(const std::string& name, const std::string& fragmentShader) {
	const std::string result = name + ".color";
	addPass(PassDesc::fullscreen(name, fragmentShader).read(0, output).write(result));
	output = result;
	return *this;
}

// Shadow atlases and G-buffer, common to every graph lighting the scene.
static void addGeometryPasses(RenderGraphConfig& graph, VkFormat worldPosFormat = VK_FORMAT_R16G16B16A16_SFLOAT) {
	using namespace shadow;

	// Sun: cascaded shadow maps, one atlas tile per cascade.
	graph.addPass(PassDesc::scene("sunShadow", "shadow.vert.spv", "")
			.writeDepth("sunShadowAtlas")
			.extent(kCascadeColumns * kCascadeResolution, kCascadeRows * kCascadeResolution)
			.views(kCascadeColumns, kCascadeRows, kCascadeViewBase)
			.depthBias(kDepthBiasConstant, kDepthBiasSlope));

	// Point lights: 6 cube faces per light, one light per atlas row.
	graph.addPass(PassDesc::scene("pointShadow", "shadow.vert.spv", "")
			.writeDepth("pointShadowAtlas")
			.extent(6 * kPointFaceResolution, kMaxPointShadows * kPointFaceResolution)
			.views(6, kMaxPointShadows, kPointViewBase)
			.depthBias(kDepthBiasConstant, kDepthBiasSlope));

	graph.addPass(PassDesc::scene("geometry", "geometry.vert.spv", "geometry.frag.spv")
			.write("gAlbedo").clear(0.2f, 0.2f, 0.2f)
			.write("gNormal", VK_FORMAT_R16G16B16A16_SFLOAT)
			.write("gWorldPos", worldPosFormat)
			.write("gMaterial", VK_FORMAT_R8G8B8A8_UNORM) // occlusion, roughness, metallic, emission
			.writeDepth("sceneDepth"));
}

// Lighting pass reading the G-buffer and the shadow atlases (bindings of lighting_common.slang).
static PassDesc lightingPass(std::string name, std::string fragmentShader) {
	PassDesc pass = PassDesc::fullscreen(std::move(name), std::move(fragmentShader));
	pass.read(0, "gAlbedo")
			.read(1, "gNormal")
			.read(2, "gWorldPos")
			.read(3, "sunShadowAtlas", SamplerKind::ShadowCompare)
			.read(4, "sunShadowAtlas", SamplerKind::Nearest) // raw depth for the PCSS blocker search
			.read(5, "pointShadowAtlas", SamplerKind::ShadowCompare)
			.read(6, "gMaterial", SamplerKind::Nearest);
	return pass;
}

// shadow -> G-buffer -> lighting -> transparency
RenderGraphConfig RenderGraphConfig::deferred() {
	RenderGraphConfig graph;
	addGeometryPasses(graph);

	graph.addPass(lightingPass("lighting", "lighting.frag.spv").write("lit"));

	graph.output = "lit";
	graph.addPostEffect("transparency", "transparency.frag.spv");
	return graph;
}

// shadow -> G-buffer -> toon lighting -> outlines -> bloom -> speed effects and tone mapping
RenderGraphConfig RenderGraphConfig::toon() {
	constexpr VkFormat hdr = VK_FORMAT_R16G16B16A16_SFLOAT;
	// The bloom is blurry by nature: small fixed images are enough at any window size.
	constexpr uint32_t bloomWidth  = 480;
	constexpr uint32_t bloomHeight = 270;

	RenderGraphConfig graph;
	// Full float positions: the grid lines are computed from them, and half floats are only
	// precise to a couple of centimeters a few dozen units away from the origin.
	addGeometryPasses(graph, VK_FORMAT_R32G32B32A32_SFLOAT);

	graph.addPass(lightingPass("toonLighting", "toon_lighting.frag.spv").write("toonLit", hdr));

	graph.addPass(PassDesc::fullscreen("outline", "toon_outline.frag.spv")
			.read(0, "toonLit")
			.read(1, "gNormal", SamplerKind::Nearest)
			.read(2, "gWorldPos", SamplerKind::Nearest)
			.write("outlined", hdr));

	// Two blur sizes: a tight glow around the emissive surfaces and a wide soft one.
	graph.addPass(PassDesc::fullscreen("bloomBright", "bloom_bright.frag.spv").read(0, "outlined").write("bloomBright", hdr).extent(bloomWidth, bloomHeight));
	graph.addPass(PassDesc::fullscreen("bloomNearH", "bloom_blur_h.frag.spv").read(0, "bloomBright").write("bloomNearH", hdr).extent(bloomWidth, bloomHeight));
	graph.addPass(PassDesc::fullscreen("bloomNearV", "bloom_blur_v.frag.spv").read(0, "bloomNearH").write("bloomNear", hdr).extent(bloomWidth, bloomHeight));
	graph.addPass(PassDesc::fullscreen("bloomWideH", "bloom_blur_h.frag.spv").read(0, "bloomNear").write("bloomWideH", hdr).extent(bloomWidth / 4, bloomHeight / 4));
	graph.addPass(PassDesc::fullscreen("bloomWideV", "bloom_blur_v.frag.spv").read(0, "bloomWideH").write("bloomWide", hdr).extent(bloomWidth / 4, bloomHeight / 4));

	graph.addPass(PassDesc::fullscreen("toonFinal", "toon_final.frag.spv")
			.read(0, "outlined")
			.read(1, "bloomNear")
			.read(2, "bloomWide")
			.write("toonFinal"));

	graph.output = "toonFinal";
	return graph;
}

} // namespace RasterCore

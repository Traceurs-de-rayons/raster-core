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

RenderGraphConfig RenderGraphConfig::deferred() {
	RenderGraphConfig graph;

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
			.write("gWorldPos", VK_FORMAT_R16G16B16A16_SFLOAT)
			.writeDepth("sceneDepth"));

	graph.addPass(PassDesc::fullscreen("lighting", "lighting.frag.spv")
			.read(0, "gAlbedo")
			.read(1, "gNormal")
			.read(2, "gWorldPos")
			.read(3, "sunShadowAtlas", SamplerKind::ShadowCompare)
			.read(4, "sunShadowAtlas", SamplerKind::Nearest) // raw depth for the PCSS blocker search
			.read(5, "pointShadowAtlas", SamplerKind::ShadowCompare)
			.write("lit"));

	graph.output = "lit";
	graph.addPostEffect("transparency", "transparency.frag.spv");
	return graph;
}

} // namespace RasterCore

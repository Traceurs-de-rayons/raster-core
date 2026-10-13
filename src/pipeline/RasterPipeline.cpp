#include "internal.hpp"

#include "renderDevice.hpp"

#include <cstring>
#include <iostream>

namespace RasterCore {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

RasterPipeline::Impl::~Impl() {
	graph.destroy();
	linearSampler.destroy();
	nearestSampler.destroy();
	if (shadowSampler != VK_NULL_HANDLE)
		vkDestroySampler(gpu->device, shadowSampler, nullptr);
	cameraBuffer.destroy();
	lightBuffer.destroy();
	lightListBuffer.destroy();
	shadowViewBuffer.destroy();
}

bool RasterPipeline::Impl::initialize(const InitOptions& initOptions, std::string& error) {
	options = initOptions;
	shadowSlots.fill(-1);

	if (!options.sharedResources || !options.sharedResources->gpu) {
		error = "RasterCore: shared GPU resources are required";
		return false;
	}
	if (options.width == 0 || options.height == 0) {
		error = "RasterCore: output resolution must be non zero";
		return false;
	}
	gpu = options.sharedResources->gpu;

	using renderApi::BufferUsage;
	cameraBuffer	 = renderApi::createUniformBuffer(gpu, sizeof(internal::CameraUniform));
	lightBuffer		 = renderApi::createUniformBuffer(gpu, sizeof(internal::LightUniform));
	lightListBuffer	 = renderApi::createStorageBuffer(gpu, sizeof(internal::GpuLight) * kMaxLights, BufferUsage::DYNAMIC);
	shadowViewBuffer = renderApi::createStorageBuffer(gpu, sizeof(internal::GpuShadowView) * shadow::kMaxShadowViews, BufferUsage::DYNAMIC);
	if (!cameraBuffer.isValid() || !lightBuffer.isValid() || !lightListBuffer.isValid() || !shadowViewBuffer.isValid()) {
		error = "RasterCore: failed to create uniform buffers";
		return false;
	}

	if (!createSamplers(error))
		return false;

	uploadUniforms();
	return buildGraph(error);
}

bool RasterPipeline::Impl::createSamplers(std::string& error) {
	renderApi::SamplerCreateInfo info{};
	info.addressModeU = renderApi::AddressMode::CLAMP_TO_EDGE;
	info.addressModeV = renderApi::AddressMode::CLAMP_TO_EDGE;
	info.addressModeW = renderApi::AddressMode::CLAMP_TO_EDGE;

	info.magFilter = info.minFilter = renderApi::FilterMode::LINEAR;
	const bool linearOk				= linearSampler.create(gpu, info);
	info.magFilter = info.minFilter = renderApi::FilterMode::NEAREST;
	const bool nearestOk			= nearestSampler.create(gpu, info);

	// Hardware PCF: each fetch returns the filtered result of (reference <= stored depth).
	VkSamplerCreateInfo shadowInfo{};
	shadowInfo.sType		 = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	shadowInfo.magFilter	 = VK_FILTER_LINEAR;
	shadowInfo.minFilter	 = VK_FILTER_LINEAR;
	shadowInfo.mipmapMode	 = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	shadowInfo.addressModeU	 = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	shadowInfo.addressModeV	 = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	shadowInfo.addressModeW	 = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	shadowInfo.compareEnable = VK_TRUE;
	shadowInfo.compareOp	 = VK_COMPARE_OP_LESS_OR_EQUAL;
	shadowInfo.maxLod		 = 0.0f;
	shadowInfo.borderColor	 = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	const bool shadowOk		 = vkCreateSampler(gpu->device, &shadowInfo, nullptr, &shadowSampler) == VK_SUCCESS;

	if (!linearOk || !nearestOk || !shadowOk) {
		error = "RasterCore: failed to create samplers";
		return false;
	}
	return true;
}

bool RasterPipeline::Impl::buildGraph(std::string& error) {
	internal::GraphContext context;
	context.gpu				  = gpu;
	context.scene			  = options.sharedResources;
	context.cameraBuffer	  = &cameraBuffer;
	context.lightBuffer		  = &lightBuffer;
	context.lightListBuffer	  = &lightListBuffer;
	context.shadowViewBuffer  = &shadowViewBuffer;
	context.samplers[static_cast<size_t>(SamplerKind::Linear)]		  = linearSampler.getHandle();
	context.samplers[static_cast<size_t>(SamplerKind::Nearest)]		  = nearestSampler.getHandle();
	context.samplers[static_cast<size_t>(SamplerKind::ShadowCompare)] = shadowSampler;
	context.colorFormat		  = options.colorFormat;
	context.width			  = options.width;
	context.height			  = options.height;
	context.shaderSearchPaths = &options.shaderSearchPaths;
	context.beforeRecord	  = [this] { flushUniforms(); };
	return graph.build(options.graph, context, error);
}

void RasterPipeline::Impl::uploadUniforms() {
	internal::CameraUniform cameraData{};
	internal::writeCameraUniform(cameraData, camera, options.width, options.height);

	const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
	internal::LightFrame frame;
	internal::buildLightFrame(frame, lights, camera, aspect, options.shadows, shadowSlots);
	frame.geometryVersion = options.sharedResources->geometryVersion;
	frame.uniform.effects = effects;

	std::lock_guard<std::mutex> lock(pendingMutex);
	pendingCamera = cameraData;
	pendingFrame  = std::move(frame);
	pendingDirty  = true;
}

// The shadow maps, the G-buffer and the lighting of one frame must all see the same camera and
// shadow views: writing them while a frame is running makes the lighting pass compare against
// shadow maps rendered from other matrices (whole cascades flash black when the camera turns).
void RasterPipeline::Impl::flushUniforms() {
	std::lock_guard<std::mutex> lock(pendingMutex);
	if (!pendingDirty)
		return;
	pendingDirty = false;

	const internal::LightFrame& frame = pendingFrame;
	cameraBuffer.upload(&pendingCamera, sizeof(pendingCamera));
	lightBuffer.upload(&frame.uniform, sizeof(frame.uniform));
	if (!frame.lights.empty())
		lightListBuffer.upload(frame.lights.data(), frame.lights.size() * sizeof(internal::GpuLight));
	shadowViewBuffer.upload(frame.shadowViews.data(), frame.shadowViews.size() * sizeof(internal::GpuShadowView));

	// Shadow maps are cached: only the tiles whose content is out of date are drawn again, and
	// not at all for a light that cannot reach the camera frustum.
	graph.setViews("sunShadow", frame.cascadeTiles, dirtyTiles(frame, shadow::kCascadeViewBase, frame.cascadeTiles));
	graph.setViews("pointShadow", frame.pointTiles, dirtyTiles(frame, shadow::kPointViewBase, frame.pointVisibleTiles));
}

// Among `tiles` (bit i = shadow view viewBase + i), the ones that no longer match what the atlas
// holds; they are recorded as up to date since the next frame draws them.
uint64_t RasterPipeline::Impl::dirtyTiles(const internal::LightFrame& frame, uint32_t viewBase, uint64_t tiles) {
	uint64_t dirty = 0;
	for (uint32_t tile = 0; viewBase + tile < shadow::kMaxShadowViews && tile < 64; ++tile) {
		if (!(tiles & (1ull << tile)))
			continue;

		const uint32_t view		= viewBase + tile;
		const auto&	   wanted	= frame.shadowViews[view];
		const bool	   sameView = renderedValid[view] && std::memcmp(&renderedViews[view], &wanted, sizeof(wanted)) == 0;
		if (sameView && renderedGeometry[view] == frame.geometryVersion)
			continue;

		renderedViews[view]	   = wanted;
		renderedGeometry[view] = frame.geometryVersion;
		renderedValid[view]	   = true;
		dirty |= 1ull << tile;
	}
	return dirty;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

InitResult initRasterisation(const InitOptions& options) {
	InitResult result;
	auto impl = std::make_unique<RasterPipeline::Impl>();
	if (!impl->initialize(options, result.errorMessage))
		return result;

	result.pipeline = std::shared_ptr<RasterPipeline>(new RasterPipeline(std::move(impl)));
	result.success	= true;
	return result;
}

RasterPipeline::RasterPipeline(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RasterPipeline::~RasterPipeline() = default;
RasterPipeline::RasterPipeline(RasterPipeline&&) noexcept = default;
RasterPipeline& RasterPipeline::operator=(RasterPipeline&&) noexcept = default;

void RasterPipeline::drawFrame() {
	if (impl_)
		impl_->uploadUniforms();
}

bool RasterPipeline::resize(uint32_t width, uint32_t height, std::string* errorMessage) {
	if (!impl_ || width == 0 || height == 0)
		return false;
	if (width == impl_->options.width && height == impl_->options.height)
		return true;

	impl_->options.width  = width;
	impl_->options.height = height;

	// Cheap path first; a full rebuild only if it fails.
	std::string error;
	if (!impl_->graph.resize(width, height, error) && !impl_->buildGraph(error)) {
		std::cerr << error << std::endl;
		if (errorMessage)
			*errorMessage = error;
		return false;
	}
	return true;
}

void RasterPipeline::setCamera(const Camera& camera) {
	if (impl_)
		impl_->camera = camera;
}

Camera RasterPipeline::getCamera() const { return impl_ ? impl_->camera : Camera{}; }

void RasterPipeline::setLights(const std::vector<Light>& lights) {
	if (impl_)
		impl_->lights = lights;
}

void RasterPipeline::setBackgroundColor(const cu::math::vec3& color) {
	if (!impl_)
		return;

	// The lighting pass shows the albedo clear color as is where nothing was drawn. Kept in the
	// config too, so a graph rebuild does not lose it.
	const std::array<float, 4> clear{color.x, color.y, color.z, 1.0f};
	for (auto& pass : impl_->options.graph.passes)
		for (auto& output : pass.colorOutputs)
			if (output.name == "gAlbedo")
				output.clearColor = clear;
	impl_->graph.setClearColor("gAlbedo", clear);
}

void RasterPipeline::setEffectParams(const std::array<std::array<float, 4>, 5>& params) {
	if (impl_)
		impl_->effects = params;
}

void RasterPipeline::setToonParams(const ToonParams& p) {
	setEffectParams({{
		{p.speedEffect, p.time, p.outlineStrength, p.bloomStrength},
		{p.fogColor.x, p.fogColor.y, p.fogColor.z, p.fogDensity},
		{p.fogHeight, p.gridStrength, p.bands, p.emissiveBrightness},
		{p.outlineColor.x, p.outlineColor.y, p.outlineColor.z, p.ambient},
		{p.sightX, p.sightY, p.sightRadius, p.sightStrength},
	}});
}

const std::vector<Light>& RasterPipeline::getLights() const {
	static const std::vector<Light> empty;
	return impl_ ? impl_->lights : empty;
}

uint32_t RasterPipeline::width() const { return impl_ ? impl_->options.width : 0; }

uint32_t RasterPipeline::height() const { return impl_ ? impl_->options.height : 0; }

renderApi::device::GPU* RasterPipeline::gpu() { return impl_ ? impl_->gpu : nullptr; }

VkImage RasterPipeline::getColorImage() const { return impl_ ? impl_->graph.outputImage() : VK_NULL_HANDLE; }

VkImageView RasterPipeline::getColorImageView() const { return impl_ ? impl_->graph.outputView() : VK_NULL_HANDLE; }

}

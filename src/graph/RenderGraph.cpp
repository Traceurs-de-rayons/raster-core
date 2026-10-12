#include "RenderGraph.hpp"

#include "Helpers.hpp"
#include "animation/Model.hpp"
#include "descriptorSetManager.hpp"
#include "gpuTask.hpp"
#include "graphicsPipeline.hpp"
#include "renderDevice.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <mutex>

namespace RasterCore::internal {

namespace {

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

bool isReservedBinding(uint32_t index, PassType type) {
	if (index == binding::Camera || index == binding::Light || index == binding::Lights || index == binding::ShadowViews)
		return true;
	if (type != PassType::Scene)
		return false;
	return index == binding::SceneTextures || index == binding::ModelMatrices || index == binding::PerDraw ||
	       index == binding::Materials || index == binding::Bones || index == binding::DrawInfo;
}

} // namespace

RenderGraph::~RenderGraph() { destroy(); }

void RenderGraph::destroy() {
	if (task_) {
		// Unregistering takes the GPU thread lock, so the task is not executing afterwards.
		task_->unregisterFromGPU();
		if (context_.gpu)
			vkDeviceWaitIdle(context_.gpu->device);
		task_.reset();
	}
	passes_.clear();
	resources_.clear();
	layouts_.clear();
	resourceIndex_.clear();
	output_ = 0;
}

// Flattens the config into passes_ and resolves every resource name.
bool RenderGraph::declarePasses(const RenderGraphConfig& config, std::string& error) {
	for (const PassDesc& passDesc : config.passes) {
		const PassDesc* desc = &passDesc;
		const size_t passIndex = passes_.size();
		CompiledPass pass;
		pass.desc = *desc;

		if (desc->name.empty()) {
			error = "RasterCore: every pass needs a name";
			return false;
		}
		if (desc->colorOutputs.empty() && desc->depthOutput.empty()) {
			error = "RasterCore: pass '" + desc->name + "' writes nothing";
			return false;
		}
		if (desc->type == PassType::Fullscreen && desc->fragmentShader.empty()) {
			error = "RasterCore: fullscreen pass '" + desc->name + "' needs a fragment shader";
			return false;
		}

		for (const auto& input : desc->inputs) {
			auto it = resourceIndex_.find(input.resource);
			if (it == resourceIndex_.end()) {
				error = "RasterCore: pass '" + desc->name + "' reads '" + input.resource + "' which no earlier pass writes";
				return false;
			}
			if (isReservedBinding(input.binding, desc->type)) {
				error = "RasterCore: pass '" + desc->name + "' uses reserved binding " + std::to_string(input.binding);
				return false;
			}
			pass.inputs.push_back(it->second);
		}

		if (desc->viewCount() > 0) {
			if (desc->width == 0 || desc->height == 0 || desc->width % desc->viewColumns || desc->height % desc->viewRows) {
				error = "RasterCore: multi-view pass '" + desc->name + "' needs a fixed extent divisible by its view grid";
				return false;
			}
			if (desc->viewCount() > 64 || !desc->colorOutputs.empty() || desc->depthOutput.empty()) {
				error = "RasterCore: multi-view pass '" + desc->name + "' must be depth only with at most 64 views";
				return false;
			}
			pass.activeViews = std::make_unique<std::atomic<uint64_t>>(0);
			pass.dirtyViews	 = std::make_unique<std::atomic<uint64_t>>(0);
		}

		auto declare = [&](const std::string& name, int attachment) {
			if (resourceIndex_.contains(name)) {
				error = "RasterCore: resource '" + name + "' is written twice";
				return false;
			}
			resourceIndex_[name] = resources_.size();
			pass.outputs.push_back(resources_.size());
			resources_.push_back({name, passIndex, attachment});
			return true;
		};
		for (size_t i = 0; i < desc->colorOutputs.size(); ++i)
			if (!declare(desc->colorOutputs[i].name, static_cast<int>(i)))
				return false;
		if (!desc->depthOutput.empty() && !declare(desc->depthOutput, -1))
			return false;

		passes_.push_back(std::move(pass));
	}

	auto it = resourceIndex_.find(config.output);
	if (it == resourceIndex_.end() || resources_[it->second].isDepth()) {
		error = "RasterCore: graph output '" + config.output + "' is not a color resource";
		return false;
	}
	output_ = it->second;
	layouts_.assign(resources_.size(), VK_IMAGE_LAYOUT_UNDEFINED);
	return true;
}

bool RenderGraph::createPipeline(CompiledPass& pass, uint32_t setIndex, const RenderGraphConfig& config, std::string& error) {
	const PassDesc& desc = pass.desc;
	const bool isScene	 = desc.type == PassType::Scene;

	std::string vertexFile = desc.vertexShader;
	if (vertexFile.empty())
		vertexFile = isScene ? config.defaultSceneVertex : config.defaultFullscreenVertex;

	std::vector<uint32_t> vertexSpirv;
	std::vector<uint32_t> fragmentSpirv;
	if (!loadShader(*context_.shaderSearchPaths, vertexFile, vertexSpirv, error))
		return false;
	if (!desc.fragmentShader.empty() && !loadShader(*context_.shaderSearchPaths, desc.fragmentShader, fragmentSpirv, error))
		return false;

	auto* pipeline = task_->createGraphicsPipeline("RasterPass_" + desc.name);
	if (!pipeline) {
		error = "RasterCore: failed to create pipeline for pass '" + desc.name + "'";
		return false;
	}

	pipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::BUFFER);
	pipeline->setDescriptorSetIndex(setIndex);
	if (desc.width && desc.height)
		pipeline->setFixedExtent(desc.width, desc.height);

	pipeline->setColorAttachmentCount(static_cast<uint32_t>(desc.colorOutputs.size()));
	for (size_t i = 0; i < desc.colorOutputs.size(); ++i) {
		const VkFormat format = desc.colorOutputs[i].format;
		pipeline->setColorAttachmentFormat(static_cast<uint32_t>(i), format == VK_FORMAT_UNDEFINED ? context_.colorFormat : format);
	}
	pipeline->setDepthFormat(kDepthFormat);
	pipeline->setDepthStencil(desc.depthTest, desc.depthTest, desc.depthTest ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS);
	pipeline->setRasterizer(VK_POLYGON_MODE_FILL, desc.cullMode, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	pipeline->setDepthBias(desc.depthBiasConstant, desc.depthBiasSlope);
	pipeline->setColorBlendAttachment(false);

	// Always dynamic: the pipeline then survives a resize (see resize()).
	pipeline->setDynamicViewport(true);
	if (desc.viewCount() > 0) {
		pipeline->setPreserveDepth(true); // tiles that are not redrawn keep their shadow map
		pipeline->addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ViewPushConstants));
	}

	if (isScene) {
		pipeline->addVertexBinding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
		pipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
		pipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal));
		pipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));
		// Skinning stream (SharedGpuResources::skinBuffer): bone indices, then weights.
		pipeline->addVertexBinding(1, sizeof(VertexSkin), VK_VERTEX_INPUT_RATE_VERTEX);
		pipeline->addVertexAttribute(3, 1, VK_FORMAT_R8G8B8A8_UINT, offsetof(VertexSkin, bones));
		pipeline->addVertexAttribute(4, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(VertexSkin, weights));
	}

	pipeline->setVertexShader(vertexSpirv);
	if (!fragmentSpirv.empty())
		pipeline->setFragmentShader(fragmentSpirv);

	pass.pipeline = pipeline;
	return true;
}

void RenderGraph::createDescriptorSet(const CompiledPass& pass, uint32_t setIndex) {
	using renderApi::descriptor::DescriptorType;
	constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	auto* set = task_->getDescriptorManager()->createSet(setIndex);
	set->addBuffer(binding::Camera, context_.cameraBuffer, DescriptorType::UNIFORM_BUFFER, stages);
	set->addBuffer(binding::Light, context_.lightBuffer, DescriptorType::UNIFORM_BUFFER, stages);
	set->addBuffer(binding::Lights, context_.lightListBuffer, DescriptorType::STORAGE_BUFFER, stages);
	set->addBuffer(binding::ShadowViews, context_.shadowViewBuffer, DescriptorType::STORAGE_BUFFER, stages);

	if (pass.desc.type == PassType::Scene) {
		auto* scene = context_.scene;
		std::vector<renderApi::Texture*> textures;
		textures.reserve(scene->textures.size());
		for (auto& texture : scene->textures)
			textures.push_back(&texture);
		set->addTextureArray(binding::SceneTextures, textures, VK_SHADER_STAGE_FRAGMENT_BIT);

		if (scene->modelMatrixBuffer.isValid())
			set->addBuffer(binding::ModelMatrices, &scene->modelMatrixBuffer, DescriptorType::STORAGE_BUFFER, stages);
		if (scene->perDrawBuffer.isValid())
			set->addBuffer(binding::PerDraw, &scene->perDrawBuffer, DescriptorType::STORAGE_BUFFER, stages);
		if (scene->materialBuffer.isValid())
			set->addBuffer(binding::Materials, &scene->materialBuffer, DescriptorType::STORAGE_BUFFER, stages);
		if (scene->boneBuffer.isValid())
			set->addBuffer(binding::Bones, &scene->boneBuffer, DescriptorType::STORAGE_BUFFER, stages);
		if (scene->drawInfoBuffer.isValid())
			set->addBuffer(binding::DrawInfo, &scene->drawInfoBuffer, DescriptorType::STORAGE_BUFFER, stages);
	}

	// Input images only exist once the task is built; see bindInputViews().
	for (const auto& input : pass.desc.inputs)
		set->addImageView(input.binding, VK_NULL_HANDLE, samplerFor(input), VK_SHADER_STAGE_FRAGMENT_BIT);
}

// Detaches every input so no descriptor keeps a view that is about to be destroyed.
void RenderGraph::clearInputViews() {
	auto* descriptorManager = task_->getDescriptorManager();
	for (size_t i = 0; i < passes_.size(); ++i) {
		auto* set = descriptorManager->getSet(static_cast<uint32_t>(i));
		for (const auto& input : passes_[i].desc.inputs)
			set->updateImageView(input.binding, VK_NULL_HANDLE, samplerFor(input));
	}
}

void RenderGraph::bindInputViews() {
	auto* descriptorManager = task_->getDescriptorManager();
	for (size_t i = 0; i < passes_.size(); ++i) {
		auto& pass = passes_[i];
		auto* set  = descriptorManager->getSet(static_cast<uint32_t>(i));
		for (size_t j = 0; j < pass.inputs.size(); ++j) {
			const auto& input = pass.desc.inputs[j];
			set->updateImageView(input.binding, viewOf(resources_[pass.inputs[j]]), samplerFor(input));
		}
		pass.descriptorSet = set->getHandle();
	}
}

bool RenderGraph::build(const RenderGraphConfig& config, const GraphContext& context, std::string& error) {
	destroy();
	context_ = context;

	if (!context_.gpu || !context_.scene || !context_.cameraBuffer || !context_.lightBuffer || !context_.lightListBuffer ||
	    !context_.shadowViewBuffer || !context_.shaderSearchPaths) {
		error = "RasterCore: incomplete render graph context";
		return false;
	}
	if (context_.scene->textures.empty()) {
		error = "RasterCore: scene has no textures (a fallback texture is required)";
		return false;
	}
	if (!declarePasses(config, error))
		return false;

	task_ = std::make_unique<renderApi::gpuTask::GpuTask>("RasterGraph", context_.gpu);
	task_->enableDescriptorManager(true);
	task_->setUseCustomRecording(true);

	for (size_t i = 0; i < passes_.size(); ++i) {
		const auto setIndex = static_cast<uint32_t>(i);
		if (!createPipeline(passes_[i], setIndex, config, error))
			return false;
		createDescriptorSet(passes_[i], setIndex);
	}

	if (!task_->build(context_.width, context_.height)) {
		error = "RasterCore: failed to build render graph task";
		return false;
	}
	bindInputViews();

	task_->addRecordingCallback([this](VkCommandBuffer cmd, uint32_t, uint32_t) {
		if (context_.beforeRecord)
			context_.beforeRecord();
		record(cmd);
	});
	task_->setAutoExecute(true);
	task_->registerWithGPU();

	std::cout << "RasterCore: render graph built (" << passes_.size() << " passes, output '" << resources_[output_].name << "')"
	          << std::endl;
	return true;
}

bool RenderGraph::resize(uint32_t width, uint32_t height, std::string& error) {
	if (!task_ || !context_.gpu) {
		error = "RasterCore: render graph is not built";
		return false;
	}

	// The GPU thread holds this lock while a frame of the task is recorded and executed.
	std::lock_guard<std::mutex> lock(context_.gpu->GpuTasksMutex);
	// The UI may still be sampling the previous output image.
	{
		std::lock_guard<std::mutex> queueLock(context_.gpu->queueMutex);
		vkDeviceWaitIdle(context_.gpu->device);
	}
	clearInputViews();

	for (auto& pass : passes_) {
		if (pass.desc.width && pass.desc.height)
			continue; // fixed extent
		if (!pass.pipeline->resize(width, height)) {
			error = "RasterCore: failed to resize pass '" + pass.desc.name + "'";
			return false;
		}
		for (size_t index : pass.outputs)
			layouts_[index] = VK_IMAGE_LAYOUT_UNDEFINED; // new images
	}
	context_.width	= width;
	context_.height = height;
	bindInputViews();
	return true;
}

VkImage RenderGraph::imageOf(const Resource& resource) const {
	const auto* pipeline = passes_[resource.pass].pipeline;
	return resource.isDepth() ? pipeline->getDepthImage() : pipeline->getColorImage(static_cast<uint32_t>(resource.attachment));
}

VkImageView RenderGraph::viewOf(const Resource& resource) const {
	const auto* pipeline = passes_[resource.pass].pipeline;
	return resource.isDepth() ? pipeline->getDepthImageView() : pipeline->getColorImageView(static_cast<uint32_t>(resource.attachment));
}

void RenderGraph::setViews(const std::string& passName, uint64_t active, uint64_t dirty) {
	for (auto& pass : passes_) {
		if (pass.desc.name != passName || !pass.activeViews)
			continue;
		const uint64_t all = pass.desc.viewCount() >= 64 ? ~0ull : (1ull << pass.desc.viewCount()) - 1;
		pass.activeViews->store(active & all, std::memory_order_relaxed);
		// Accumulated: a frame that is never recorded must not lose its redraws.
		pass.dirtyViews->fetch_or(dirty & active & all, std::memory_order_relaxed);
	}
}

void RenderGraph::setClearColor(const std::string& resource, const std::array<float, 4>& color) {
	const auto it = resourceIndex_.find(resource);
	if (it == resourceIndex_.end() || resources_[it->second].isDepth())
		return;

	const Resource&				target = resources_[it->second];
	std::lock_guard<std::mutex> lock(clearMutex_);
	passes_[target.pass].desc.colorOutputs[static_cast<size_t>(target.attachment)].clearColor = color;
}

VkImage RenderGraph::outputImage() const { return task_ ? imageOf(resources_[output_]) : VK_NULL_HANDLE; }

VkImageView RenderGraph::outputView() const { return task_ ? viewOf(resources_[output_]) : VK_NULL_HANDLE; }

// ---------------------------------------------------------------------------
// Recording (GPU thread)
// ---------------------------------------------------------------------------

namespace {

// Access of an image in `layout`, as the source or the destination of a transition.
void layoutAccess(VkImageLayout layout, bool depth, bool source, VkAccessFlags& access, VkPipelineStageFlags& stage) {
	if (layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		access = 0;
		stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	} else if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | (source ? 0 : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	} else if (layout == VK_IMAGE_LAYOUT_GENERAL && source) {
		// Color attachments are left in GENERAL by their render pass.
		access = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else {
		access = VK_ACCESS_SHADER_READ_BIT;
		stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, bool depth, VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkImageMemoryBarrier barrier{};
	barrier.sType						= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout					= oldLayout;
	barrier.newLayout					= newLayout;
	barrier.srcQueueFamilyIndex			= VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex			= VK_QUEUE_FAMILY_IGNORED;
	barrier.image						= image;
	barrier.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage = 0;
	VkPipelineStageFlags dstStage = 0;
	layoutAccess(oldLayout, depth, true, barrier.srcAccessMask, srcStage);
	layoutAccess(newLayout, depth, false, barrier.dstAccessMask, dstStage);
	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

void RenderGraph::record(VkCommandBuffer cmd) const {
	for (const auto& pass : passes_) {
		for (size_t index : pass.inputs) {
			if (layouts_[index] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				continue;
			const auto& resource = resources_[index];
			imageBarrier(cmd, imageOf(resource), resource.isDepth(), layouts_[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			layouts_[index] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		recordPass(cmd, pass);
	}

	// The viewport samples the output in GENERAL layout from the ImGui submission.
	imageBarrier(cmd, imageOf(resources_[output_]), false, layouts_[output_], VK_IMAGE_LAYOUT_GENERAL);
	layouts_[output_] = VK_IMAGE_LAYOUT_GENERAL;
}

void RenderGraph::recordPass(VkCommandBuffer cmd, const CompiledPass& pass) const {
	const auto* pipeline = pass.pipeline;

	// Multi-view passes keep their content: only the requested tiles are drawn, and the whole
	// pass is skipped when there is none.
	uint64_t views = 0;
	if (pass.activeViews) {
		const size_t atlas = pass.outputs[0];
		views = pass.dirtyViews->exchange(0, std::memory_order_relaxed);
		if (layouts_[atlas] == VK_IMAGE_LAYOUT_UNDEFINED)
			views = pass.activeViews->load(std::memory_order_relaxed); // nothing valid yet
		if (views == 0)
			return;

		// The render pass loads the atlas, which a previous frame left ready to be sampled.
		if (layouts_[atlas] != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			imageBarrier(cmd, imageOf(resources_[atlas]), true, layouts_[atlas], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	std::vector<VkClearValue> clearValues;
	{
		std::lock_guard<std::mutex> lock(clearMutex_);
		for (const auto& output : pass.desc.colorOutputs) {
			VkClearValue value{};
			std::copy(output.clearColor.begin(), output.clearColor.end(), value.color.float32);
			clearValues.push_back(value);
		}
	}
	VkClearValue depthClear{};
	depthClear.depthStencil = {1.0f, 0};
	clearValues.push_back(depthClear);

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType				= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass		= pipeline->getRenderPass();
	beginInfo.framebuffer		= pipeline->getFramebuffer();
	beginInfo.renderArea.extent = {pipeline->getWidth(), pipeline->getHeight()};
	beginInfo.clearValueCount	= static_cast<uint32_t>(clearValues.size());
	beginInfo.pClearValues		= clearValues.data();

	vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getLayout(), 0, 1, &pass.descriptorSet, 0, nullptr);

	if (pass.activeViews) {
		recordViews(cmd, pass, views);
	} else {
		const VkViewport viewport{0.0f, 0.0f, static_cast<float>(pipeline->getWidth()), static_cast<float>(pipeline->getHeight()), 0.0f, 1.0f};
		const VkRect2D	 scissor{{0, 0}, {pipeline->getWidth(), pipeline->getHeight()}};
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		drawPassGeometry(cmd, pass);
	}

	vkCmdEndRenderPass(cmd);

	// Layouts the render pass leaves its attachments in (see render-api pipelineBuild.cpp).
	for (size_t index : pass.outputs)
		layouts_[index] = resources_[index].isDepth() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}

// One draw per requested atlas tile, each cleared first and drawn with its own viewport and view index.
void RenderGraph::recordViews(VkCommandBuffer cmd, const CompiledPass& pass, uint64_t views) const {
	const PassDesc& desc	   = pass.desc;
	const uint32_t	tileWidth  = desc.width / desc.viewColumns;
	const uint32_t	tileHeight = desc.height / desc.viewRows;

	for (uint32_t i = 0; i < desc.viewCount(); ++i) {
		if (!(views & (1ull << i)))
			continue;

		const uint32_t x = (i % desc.viewColumns) * tileWidth;
		const uint32_t y = (i / desc.viewColumns) * tileHeight;

		const VkViewport viewport{static_cast<float>(x), static_cast<float>(y), static_cast<float>(tileWidth), static_cast<float>(tileHeight),
								  0.0f, 1.0f};
		const VkRect2D	 scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {tileWidth, tileHeight}};
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkClearAttachment clear{};
		clear.aspectMask			  = VK_IMAGE_ASPECT_DEPTH_BIT;
		clear.clearValue.depthStencil = {1.0f, 0};
		const VkClearRect clearRect{scissor, 0, 1};
		vkCmdClearAttachments(cmd, 1, &clear, 1, &clearRect);

		const ViewPushConstants constants{desc.viewBase + i};
		vkCmdPushConstants(cmd, pass.pipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants), &constants);
		drawPassGeometry(cmd, pass);
	}
}

void RenderGraph::drawPassGeometry(VkCommandBuffer cmd, const CompiledPass& pass) const {
	if (pass.desc.type == PassType::Scene)
		drawScene(cmd);
	else
		vkCmdDraw(cmd, 3, 1, 0, 0);
}

void RenderGraph::drawScene(VkCommandBuffer cmd) const {
	const auto* scene = context_.scene;
	if (!scene->vertexBuffer.isValid() || !scene->skinBuffer.isValid())
		return;

	const VkBuffer	   vertexBuffers[2] = {scene->vertexBuffer.getHandle(), scene->skinBuffer.getHandle()};
	const VkDeviceSize offsets[2]		= {0, 0};
	vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);

	if (!scene->indexBuffer.isValid())
		return;
	vkCmdBindIndexBuffer(cmd, scene->indexBuffer.getHandle(), 0, VK_INDEX_TYPE_UINT32);

	if (scene->drawCount > 0 && scene->drawCommandsBuffer.isValid())
		vkCmdDrawIndexedIndirect(cmd, scene->drawCommandsBuffer.getHandle(), 0, scene->drawCount, sizeof(VkDrawIndexedIndirectCommand));
	else
		vkCmdDrawIndexed(cmd, scene->indexCount, 1, 0, 0, 0);
}

} // namespace RasterCore::internal

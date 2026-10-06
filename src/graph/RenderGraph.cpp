#include "RenderGraph.hpp"

#include "Helpers.hpp"
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
	       index == binding::Materials;
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
			pass.activeViews = std::make_unique<std::atomic<uint32_t>>(desc->viewCount());
		}

		auto declare = [&](const std::string& name, int attachment) {
			if (resourceIndex_.contains(name)) {
				error = "RasterCore: resource '" + name + "' is written twice";
				return false;
			}
			resourceIndex_[name] = resources_.size();
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
		pipeline->addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ViewPushConstants));
	}

	if (isScene) {
		pipeline->addVertexBinding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
		pipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
		pipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal));
		pipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv));
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

void RenderGraph::setActiveViews(const std::string& passName, uint32_t count) {
	for (auto& pass : passes_) {
		if (pass.desc.name == passName && pass.activeViews)
			pass.activeViews->store(std::min(count, pass.desc.viewCount()), std::memory_order_relaxed);
	}
}

VkImage RenderGraph::outputImage() const { return task_ ? imageOf(resources_[output_]) : VK_NULL_HANDLE; }

VkImageView RenderGraph::outputView() const { return task_ ? viewOf(resources_[output_]) : VK_NULL_HANDLE; }

// ---------------------------------------------------------------------------
// Recording (GPU thread)
// ---------------------------------------------------------------------------

namespace {

// Layout each attachment is left in by its render pass (see render-api pipelineBuild.cpp).
VkImageLayout attachmentLayout(bool depth) {
	return depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
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
	barrier.srcAccessMask = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	const VkPipelineStageFlags srcStage = depth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
	                                            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

void RenderGraph::record(VkCommandBuffer cmd) const {
	// Every resource is rewritten each frame, so layouts only need tracking within the frame.
	std::vector<bool> sampled(resources_.size(), false);

	for (const auto& pass : passes_) {
		for (size_t index : pass.inputs) {
			if (sampled[index])
				continue;
			const auto& resource = resources_[index];
			imageBarrier(cmd, imageOf(resource), resource.isDepth(), attachmentLayout(resource.isDepth()),
			             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			sampled[index] = true;
		}
		recordPass(cmd, pass);
	}

	// The viewport samples the output in GENERAL layout from the ImGui submission.
	const auto& output = resources_[output_];
	const VkImageLayout current = sampled[output_] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
	imageBarrier(cmd, imageOf(output), false, current, VK_IMAGE_LAYOUT_GENERAL);
}

void RenderGraph::recordPass(VkCommandBuffer cmd, const CompiledPass& pass) const {
	const auto* pipeline = pass.pipeline;

	std::vector<VkClearValue> clearValues;
	for (const auto& output : pass.desc.colorOutputs) {
		VkClearValue value{};
		std::copy(output.clearColor.begin(), output.clearColor.end(), value.color.float32);
		clearValues.push_back(value);
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
		recordViews(cmd, pass);
	} else {
		const VkViewport viewport{0.0f, 0.0f, static_cast<float>(pipeline->getWidth()), static_cast<float>(pipeline->getHeight()), 0.0f, 1.0f};
		const VkRect2D	 scissor{{0, 0}, {pipeline->getWidth(), pipeline->getHeight()}};
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		drawPassGeometry(cmd, pass);
	}

	vkCmdEndRenderPass(cmd);
}

// One draw per active atlas tile, each with its own viewport and view index.
void RenderGraph::recordViews(VkCommandBuffer cmd, const CompiledPass& pass) const {
	const PassDesc& desc	   = pass.desc;
	const uint32_t	tileWidth  = desc.width / desc.viewColumns;
	const uint32_t	tileHeight = desc.height / desc.viewRows;
	const uint32_t	count	   = pass.activeViews->load(std::memory_order_relaxed);

	for (uint32_t i = 0; i < count; ++i) {
		const uint32_t x = (i % desc.viewColumns) * tileWidth;
		const uint32_t y = (i / desc.viewColumns) * tileHeight;

		const VkViewport viewport{static_cast<float>(x), static_cast<float>(y), static_cast<float>(tileWidth), static_cast<float>(tileHeight),
								  0.0f, 1.0f};
		const VkRect2D	 scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {tileWidth, tileHeight}};
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

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
	if (!scene->vertexBuffer.isValid())
		return;

	VkBuffer	 vertexBuffer = scene->vertexBuffer.getHandle();
	VkDeviceSize offset		  = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

	if (!scene->indexBuffer.isValid())
		return;
	vkCmdBindIndexBuffer(cmd, scene->indexBuffer.getHandle(), 0, VK_INDEX_TYPE_UINT32);

	if (scene->drawCount > 0 && scene->drawCommandsBuffer.isValid())
		vkCmdDrawIndexedIndirect(cmd, scene->drawCommandsBuffer.getHandle(), 0, scene->drawCount, sizeof(VkDrawIndexedIndirectCommand));
	else
		vkCmdDrawIndexed(cmd, scene->indexCount, 1, 0, 0, 0);
}

} // namespace RasterCore::internal

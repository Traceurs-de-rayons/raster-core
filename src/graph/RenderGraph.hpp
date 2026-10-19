#pragma once

#include "rasterCore.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderApi::gpuTask {
class GpuTask;
class GraphicsPipeline;
}

namespace RasterCore::internal {

// Everything a graph needs from its owner; the pointed resources must outlive the graph.
struct GraphContext {
	renderApi::device::GPU*					  gpu		   = nullptr;
	SharedGpuResources*						  scene		   = nullptr;
	renderApi::Buffer*						  cameraBuffer = nullptr;
	renderApi::Buffer*						  lightBuffer  = nullptr;
	renderApi::Buffer*						  lightListBuffer = nullptr;
	renderApi::Buffer*						  shadowViewBuffer = nullptr;
	std::array<VkSampler, 3>				  samplers{}; // indexed by SamplerKind
	VkFormat								  colorFormat  = VK_FORMAT_R8G8B8A8_UNORM;
	uint32_t								  width		   = 0;
	uint32_t								  height	   = 0;
	const std::vector<std::filesystem::path>* shaderSearchPaths = nullptr;
	// Called on the GPU thread before each frame is recorded, once the previous one has finished:
	// the only point where the buffers read by the passes can change without tearing a frame.
	std::function<void()>					  beforeRecord;
};

// Compiles a RenderGraphConfig into one GpuTask: one pipeline + one descriptor set per pass,
// recorded in a single command buffer with the layout barriers between passes.
class RenderGraph {
  public:
	RenderGraph() = default;
	~RenderGraph();

	RenderGraph(const RenderGraph&)			   = delete;
	RenderGraph& operator=(const RenderGraph&) = delete;

	bool build(const RenderGraphConfig& config, const GraphContext& context, std::string& error);
	void destroy();

	// New output size: only the screen-sized attachments are recreated (pipelines, shaders and
	// fixed-extent passes such as the shadow atlases are kept).
	bool resize(uint32_t width, uint32_t height, std::string& error);

	// The buffers or textures of the scene (GraphContext::scene) were replaced: the scene passes
	// read them from their next frame. Only while no frame of the task is recorded or executing
	// (the caller holds GPU::GpuTasksMutex).
	void refreshSceneBindings();

	VkImage		outputImage() const;
	VkImageView outputView() const;

	// Views (one bit per tile) of a multi-view pass: `active` are the tiles in use, `dirty` the ones
	// to draw again in the next recorded frame; the others keep what they already contain.
	// Safe to call from any thread.
	void setViews(const std::string& passName, uint64_t active, uint64_t dirty);

	// Clear color of a color resource, used from the next recorded frame. Safe to call from any thread.
	void setClearColor(const std::string& resource, const std::array<float, 4>& color);

  private:
	struct Resource {
		std::string name;
		size_t		pass	   = 0;
		int			attachment = 0; // color attachment index, -1 = depth
		bool		isDepth() const { return attachment < 0; }
	};

	struct CompiledPass {
		PassDesc							  desc;
		renderApi::gpuTask::GraphicsPipeline* pipeline		= nullptr;
		VkDescriptorSet						  descriptorSet = VK_NULL_HANDLE;
		std::vector<size_t>					  inputs; // resource indices
		std::vector<size_t>					  outputs; // resource indices
		// Multi-view passes only: bit masks of the tiles in use / to redraw (see setViews()).
		std::unique_ptr<std::atomic<uint64_t>> activeViews;
		std::unique_ptr<std::atomic<uint64_t>> dirtyViews;
	};

	GraphContext							 context_;
	std::unique_ptr<renderApi::gpuTask::GpuTask> task_;
	std::vector<CompiledPass>				 passes_;
	std::vector<Resource>					 resources_;
	std::unordered_map<std::string, size_t>	 resourceIndex_;
	size_t									 output_ = 0;
	// Layout each resource was left in by the last recorded frame (GPU thread only).
	mutable std::vector<VkImageLayout>		 layouts_;
	mutable std::mutex						 clearMutex_; // clear colors are set by the app, read by the GPU thread

	bool declarePasses(const RenderGraphConfig& config, std::string& error);
	bool createPipeline(CompiledPass& pass, uint32_t setIndex, const RenderGraphConfig& config, std::string& error);
	void createDescriptorSet(const CompiledPass& pass, uint32_t setIndex);
	void bindInputViews();
	void clearInputViews();

	VkSampler	samplerFor(const PassInput& input) const { return context_.samplers[static_cast<size_t>(input.sampler)]; }
	VkImage		imageOf(const Resource& resource) const;
	VkImageView viewOf(const Resource& resource) const;

	void record(VkCommandBuffer cmd) const;
	void recordPass(VkCommandBuffer cmd, const CompiledPass& pass) const;
	void recordViews(VkCommandBuffer cmd, const CompiledPass& pass, uint64_t views) const;
	void drawPassGeometry(VkCommandBuffer cmd, const CompiledPass& pass) const;
	void drawScene(VkCommandBuffer cmd) const;
};

} // namespace RasterCore::internal

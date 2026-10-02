#pragma once

#include "rasterCore.hpp"

#include <vulkan/vulkan_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
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

	VkImage		outputImage() const;
	VkImageView outputView() const;

	// Number of views a multi-view pass draws next frame (clamped to its view count).
	// Safe to call from any thread.
	void setActiveViews(const std::string& passName, uint32_t count);

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
		std::unique_ptr<std::atomic<uint32_t>> activeViews; // multi-view passes only
	};

	GraphContext							 context_;
	std::unique_ptr<renderApi::gpuTask::GpuTask> task_;
	std::vector<CompiledPass>				 passes_;
	std::vector<Resource>					 resources_;
	std::unordered_map<std::string, size_t>	 resourceIndex_;
	size_t									 output_ = 0;

	bool declarePasses(const RenderGraphConfig& config, std::string& error);
	bool createPipeline(CompiledPass& pass, uint32_t setIndex, const RenderGraphConfig& config, std::string& error);
	void createDescriptorSet(const CompiledPass& pass, uint32_t setIndex);
	void bindInputViews();

	VkSampler	samplerFor(const PassInput& input) const { return context_.samplers[static_cast<size_t>(input.sampler)]; }
	VkImage		imageOf(const Resource& resource) const;
	VkImageView viewOf(const Resource& resource) const;

	void record(VkCommandBuffer cmd) const;
	void recordPass(VkCommandBuffer cmd, const CompiledPass& pass) const;
	void recordViews(VkCommandBuffer cmd, const CompiledPass& pass) const;
	void drawPassGeometry(VkCommandBuffer cmd, const CompiledPass& pass) const;
	void drawScene(VkCommandBuffer cmd) const;
};

} // namespace RasterCore::internal

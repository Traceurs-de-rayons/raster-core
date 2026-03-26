#pragma once

#include "viewportManager.hpp"
#include "rasterCore.hpp"
#include "buffer.hpp"
#include "image.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace RasterCore {

class Viewport;

struct ViewportManager::Impl {
	std::vector<std::unique_ptr<Viewport>> viewports;
	std::unordered_map<std::string, size_t> nameToIndex;
	std::unordered_map<uint32_t, size_t> idToIndex;
	SharedGpuResources sharedResources;
	int nextUnnamedId;

	renderApi::device::GPU* sharedGpu = nullptr;
	renderApi::Buffer sharedVertexBuffer;
	renderApi::Buffer sharedIndexBuffer;
	renderApi::Buffer sharedModelMatrixBuffer;
	std::vector<renderApi::Texture> sharedTextures;

	Impl();
	~Impl();

	std::string generateName();
	void updateIndices();
	void removeViewportAtIndex(size_t index);

	bool createSharedGpuIfNeeded();
	void updateSharedResources();
	void destroySharedResources();
};

}

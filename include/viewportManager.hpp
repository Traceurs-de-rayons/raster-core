#pragma once

#include "rasterTypes.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct SDL_Window;

namespace RasterCore {

class Viewport;
struct SharedGpuResources;

class ViewportManager {
public:
	ViewportManager();
	~ViewportManager();

	// Initialize internal GPU and SharedGpuResources if needed.
	// Safe to call multiple times; subsequent calls are no-ops.
	bool init();

	// Disable copy, enable move
	ViewportManager(const ViewportManager&) = delete;
	ViewportManager& operator=(const ViewportManager&) = delete;
	ViewportManager(ViewportManager&&) noexcept;
	ViewportManager& operator=(ViewportManager&&) noexcept;

	Viewport* addViewport(SDL_Window* window, const std::string& name = "");
	Viewport* addViewport(uint32_t width, uint32_t height, const std::string& name = "");

	Viewport* getViewport(const std::string& name);
	Viewport* getViewport(uint32_t id);

	bool removeViewport(const std::string& name);
	bool removeViewport(uint32_t id);

	const std::vector<std::unique_ptr<Viewport>>& getViewports() const;

	// Access the internally managed SharedGpuResources.
	// Returns nullptr if init() has not successfully created a GPU.
	SharedGpuResources* getSharedResources();
	const SharedGpuResources* getSharedResources() const;

	// Set externally-created shared GPU resources (no internal Scene conversion).
	// The ViewportManager will forward this SharedGpuResources pointer to new viewports.
	void setSharedResources(SharedGpuResources* resources);

	void renderAll();

	size_t getViewportCount() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace RasterCore

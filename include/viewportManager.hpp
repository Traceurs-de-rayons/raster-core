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

	bool init();

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

	SharedGpuResources* getSharedResources();
	const SharedGpuResources* getSharedResources() const;

	void setSharedResources(SharedGpuResources* resources);

	void renderAll();

	void pauseAllViewportTasks();
	void resumeAllViewportTasks();

	size_t getViewportCount() const;
	void check_resize();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}

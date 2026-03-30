#pragma once

#include "viewport.hpp"
#include "rasterCore.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <string>

namespace RasterCore {

class RasterPipeline;
class ViewportManager;

struct Viewport::Impl {
	uint32_t						id;
	std::string						name;
	uint32_t						width;
	uint32_t						height;
	ViewportOutput					outputType;
	SDL_Window*						window;
	RenderMode						renderMode;
	bool							active;
	Camera							camera;
	SharedGpuResources*				sharedResources;
	ViewportManager*				viewportManager;
	std::shared_ptr<RasterPipeline>	pipeline;
	static uint32_t					nextId;

	Impl(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window, SharedGpuResources* sharedResources, ViewportManager* viewportManager);

	bool initializePipeline();
	static std::unique_ptr<Viewport> create(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window = nullptr, SharedGpuResources* sharedResources = nullptr, ViewportManager* viewportManager = nullptr);
};

}

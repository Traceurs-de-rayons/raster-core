#pragma once

#include "viewport.hpp"
#include "rasterCore.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <string>

namespace RasterCore {

class RasterPipeline;
struct Scene;

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
	std::shared_ptr<RasterPipeline>	pipeline;
	static uint32_t					nextId;

	Impl(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window, SharedGpuResources* sharedResources);

	bool initializePipeline();
	static std::unique_ptr<Viewport> create(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window = nullptr, SharedGpuResources* sharedResources = nullptr);
};

}

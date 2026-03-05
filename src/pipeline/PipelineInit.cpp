#include "Internal.hpp"

#include <iostream>

namespace RasterCore {

bool RasterPipeline::Impl::initialize(const Scene& sceneInput, InitOptions initOptions, std::string& errorMessage) {
	options = std::move(initOptions);
	shaderConfig = options.shaders;
	target = options.target;
	bufferConfig = options.buffer;
	windowConfig = options.window;
	scene = sceneInput;
	activeCamera = sceneInput.camera;
	modelTransform = cu::math::mat4::identity();

	if (target == OutputTarget::Window) {
		if (!windowConfig.window) {
			errorMessage = "RasterCore: window output requested but SDL_Window* is null";
			return false;
		}
		int w = 0;
		int h = 0;
		SDL_GetWindowSize(windowConfig.window, &w, &h);
		if (w <= 0 || h <= 0) {
			errorMessage = "RasterCore: SDL window has invalid size";
			return false;
		}
		width = static_cast<uint32_t>(w);
		height = static_cast<uint32_t>(h);
		colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
	} else {
		if (bufferConfig.width == 0 || bufferConfig.height == 0) {
			errorMessage = "RasterCore: buffer output requires non zero resolution";
			return false;
		}
		width = bufferConfig.width;
		height = bufferConfig.height;
		colorFormat = bufferConfig.colorFormat;
	}

	if (shaderConfig.searchPaths.empty()) {
		if (std::string(RASTER_CORE_SHADER_DIR).size() > 0) {
			shaderConfig.searchPaths.push_back(std::filesystem::path(RASTER_CORE_SHADER_DIR));
		}
		if (std::string(RASTER_CORE_SHADER_FALLBACK_DIR).size() > 0) {
			shaderConfig.searchPaths.push_back(std::filesystem::path(RASTER_CORE_SHADER_FALLBACK_DIR));
		}
		shaderConfig.searchPaths.push_back(std::filesystem::path("shaders"));
	}


	if (!options.sharedResources || !options.sharedResources->gpu) {
		errorMessage = "RasterCore: shared GPU is required";
		return false;
	}

	gpu = options.sharedResources->gpu;
	std::cout << "RasterCore: Using shared GPU" << std::endl;

	return rebuildPipeline(errorMessage);
}

}

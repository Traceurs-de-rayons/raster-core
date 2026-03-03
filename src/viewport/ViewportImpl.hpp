#pragma once

#include "Viewport.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <string>

namespace RasterCore {

class RasterPipeline;
struct Scene;

// Private implementation for Viewport
struct Viewport::Impl {
    uint32_t id;
    std::string name;
    uint32_t width;
    uint32_t height;
    ViewportOutput outputType;
    SDL_Window* window;
    RenderMode renderMode;
    bool active;
    Camera camera;
    
    std::shared_ptr<RasterPipeline> pipeline;
    
    static uint32_t nextId;
    
    Impl(uint32_t id, const std::string& name, uint32_t width, uint32_t height,
         ViewportOutput outputType, SDL_Window* window);
    
    bool initializePipeline(const Scene& scene);
    
    // Factory method for creating viewports with private constructor
    static std::unique_ptr<Viewport> create(uint32_t id, const std::string& name,
                                            uint32_t width, uint32_t height,
                                            ViewportOutput outputType, SDL_Window* window = nullptr);
};

} // namespace RasterCore
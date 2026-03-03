#pragma once

#include "RasterTypes.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
struct SDL_Window;

namespace RasterCore {

// Forward declarations
class Viewport;
struct Scene;

// ViewportManager - manages all viewports
class ViewportManager {
public:
    ViewportManager();
    ~ViewportManager();

    // Disable copy, enable move
    ViewportManager(const ViewportManager&) = delete;
    ViewportManager& operator=(const ViewportManager&) = delete;
    ViewportManager(ViewportManager&&) noexcept;
    ViewportManager& operator=(ViewportManager&&) noexcept;

    // Add a new viewport rendering to an SDL window
    Viewport* addViewport(SDL_Window* window, const std::string& name = "");

    // Add a new viewport rendering to a buffer
    Viewport* addViewport(uint32_t width, uint32_t height, const std::string& name = "");

    // Get viewport by name or ID
    Viewport* getViewport(const std::string& name);
    Viewport* getViewport(uint32_t id);

    // Remove viewport
    bool removeViewport(const std::string& name);
    bool removeViewport(uint32_t id);

    // Get all viewports
    const std::vector<std::unique_ptr<Viewport>>& getViewports() const;

    // Update scene for all viewports
    void updateScene(const Scene& scene);

    // Render all active viewports
    void renderAll();

    // Get viewport count
    size_t getViewportCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RasterCore
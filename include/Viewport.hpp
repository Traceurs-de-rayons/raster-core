#pragma once

#include "RasterTypes.hpp"
#include <cstdint>
#include <string>
#include <memory>

// Forward declarations
struct SDL_Window;

namespace RasterCore {

// Forward declarations
class RasterPipeline;
struct Scene;

// Viewport class - represents a single rendering viewport
// Each viewport has its own integrated camera
class Viewport {
public:
    ~Viewport();

    // Disable copy, enable move
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport(Viewport&&) noexcept;
    Viewport& operator=(Viewport&&) noexcept;

    // Viewport properties
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    uint32_t getId() const;
    const std::string& getName() const;

    // Rendering control
    void setRenderMode(RenderMode mode);
    RenderMode getRenderMode() const;

    void setActive(bool active);
    bool isActive() const;

    // Camera access (each viewport has its own integrated camera)
    Camera& getCamera();
    const Camera& getCamera() const;

    // Scene update
    void updateScene(const Scene& scene);

    // Render this viewport
    void render();

    // Access to underlying pipeline (advanced usage)
    RasterPipeline* getPipeline();
    const RasterPipeline* getPipeline() const;

    // Get rendered image data (only for Buffer output)
    const unsigned char* getImageData() const;
    size_t getImageDataSize() const;
    
    // Get Vulkan image for ImGui display (only for Buffer output)
    void* getVulkanImage() const; // Returns VkImage
    void* getVulkanImageView() const; // Returns VkImageView

private:
    friend class ViewportManager;
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Private constructor - only ViewportManager can create viewports
    Viewport(uint32_t id, const std::string& name, uint32_t width, uint32_t height,
             ViewportOutput outputType, SDL_Window* window = nullptr);
};

} // namespace RasterCore
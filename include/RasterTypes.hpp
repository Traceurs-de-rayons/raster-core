#pragma once

#include "core-utils.hpp"
#include <cstdint>

namespace RasterCore {

// Camera structure
struct Camera {
	cu::math::vec3 position{0.0f, 0.0f, 5.0f};
	cu::math::vec3 target{0.0f, 0.0f, 0.0f};
	cu::math::vec3 up{0.0f, 1.0f, 0.0f};
	float verticalFovDegrees = 60.0f;
	float nearPlane = 0.1f;
	float farPlane = 1000.0f;
};

// Render modes for viewport
enum class RenderMode {
	Normal,	  // Standard PBR rendering
	Wireframe,   // Wireframe only
	Normals,	 // Show normals as colors
	UVs,		 // Show UVs as colors
	Depth,	   // Depth visualization
};

// Viewport output target
enum class ViewportOutput {
	Window,	  // Render to SDL window
	Buffer,	  // Render to CPU-accessible buffer
};

// Output target type
enum class OutputTarget { 
	Buffer, 
	Window 
};

} // namespace RasterCore
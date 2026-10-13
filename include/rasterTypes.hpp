#pragma once

#include "math/vec3.hpp"


namespace RasterCore {

struct Camera {
	cu::math::vec3 position{0.0f, 0.0f, 5.0f};
	cu::math::vec3 target{0.0f, 0.0f, 0.0f};
	cu::math::vec3 up{0.0f, 1.0f, 0.0f};
	float verticalFovDegrees = 60.0f;
	// Field of view the view models are drawn with (first person arms keep their shape when the
	// player changes the field of view of the game); 0 = same as verticalFovDegrees.
	float viewModelFovDegrees = 0.0f;
	// False leaves the view models out of what this camera sees: the picture inside a scope
	// must not show the rifle it sits on.
	bool drawViewModels = true;
	float nearPlane = 0.1f;
	float farPlane = 10000.0f;
};

}

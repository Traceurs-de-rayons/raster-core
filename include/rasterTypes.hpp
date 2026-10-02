#pragma once

#include "math/vec3.hpp"


namespace RasterCore {

struct Camera {
	cu::math::vec3 position{0.0f, 0.0f, 5.0f};
	cu::math::vec3 target{0.0f, 0.0f, 0.0f};
	cu::math::vec3 up{0.0f, 1.0f, 0.0f};
	float verticalFovDegrees = 60.0f;
	float nearPlane = 0.1f;
	float farPlane = 10000.0f;
};

}

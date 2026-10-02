#include "internal.hpp"

#include "Helpers.hpp"
#include "math/transform.hpp"

#include <algorithm>
#include <cmath>

// Matrices follow cu::math's layout (m[column][row], like GLM) and Vulkan clip space (depth 0..1).

namespace RasterCore::internal {

using cu::math::mat4;
using cu::math::vec3;

namespace {

	// Distance kept behind each cascade so casters outside the view still land in the map.
	constexpr float kCasterMargin = 100.0f;
	constexpr float kPointNear	  = 0.05f;
	// Extra field of view per cube face so filtering near an edge stays inside the face.
	constexpr float kCubeGuardTexels = 4.0f;

	mat4 orthographic(float halfSize, float nearPlane, float farPlane) {
		mat4 m(0.0f);
		m.m[0][0] = 1.0f / halfSize;
		m.m[1][1] = 1.0f / halfSize;
		m.m[2][2] = -1.0f / (farPlane - nearPlane);
		m.m[3][2] = -nearPlane / (farPlane - nearPlane);
		m.m[3][3] = 1.0f;
		return m;
	}

	// Symmetric perspective with depth 0..1; `tanHalfFov` > 1 widens past 90 degrees.
	mat4 perspectiveZeroToOne(float tanHalfFov, float nearPlane, float farPlane) {
		mat4 m(0.0f);
		m.m[0][0] = 1.0f / tanHalfFov;
		m.m[1][1] = 1.0f / tanHalfFov;
		m.m[2][2] = farPlane / (nearPlane - farPlane);
		m.m[2][3] = -1.0f;
		m.m[3][2] = nearPlane * farPlane / (nearPlane - farPlane);
		return m;
	}

	vec3 stableUp(const vec3& direction) {
		return std::abs(direction.y) > 0.99f ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 1.0f, 0.0f};
	}

	void writeView(GpuShadowView& out, const mat4& view, const mat4& proj, uint32_t tile, uint32_t columns, uint32_t rows) {
		out.view	  = toFloatArray(view);
		out.proj	  = toFloatArray(proj);
		out.atlasRect = {static_cast<float>(tile % columns) / columns, static_cast<float>(tile / columns) / rows, 1.0f / columns, 1.0f / rows};
	}

	// Camera frustum slice [nearDepth, farDepth] as a bounding sphere (stable under rotation).
	void frustumSliceSphere(const Camera& camera, float aspect, float nearDepth, float farDepth, vec3& center, float& radius) {
		const vec3	forward = vec3::normalize(camera.target - camera.position);
		const vec3	right	= vec3::normalize(vec3::cross(forward, camera.up));
		const vec3	up		= vec3::cross(right, forward);
		const float tanHalf = std::tan(camera.verticalFovDegrees * 0.5f * 3.14159265f / 180.0f);

		vec3 corners[8];
		int	 index = 0;
		for (float depth : {nearDepth, farDepth}) {
			const float halfHeight = depth * tanHalf;
			const float halfWidth  = halfHeight * aspect;
			const vec3	middle	   = camera.position + forward * depth;
			for (float sx : {-1.0f, 1.0f})
				for (float sy : {-1.0f, 1.0f})
					corners[index++] = middle + right * (sx * halfWidth) + up * (sy * halfHeight);
		}

		center = vec3(0.0f);
		for (const vec3& corner : corners)
			center = center + corner;
		center = center * (1.0f / 8.0f);

		radius = 0.0f;
		for (const vec3& corner : corners)
			radius = std::max(radius, (corner - center).length());
		radius = std::ceil(radius * 16.0f) / 16.0f; // quantized so the texel size does not jitter
	}

}

void writeCascades(LightFrame& frame, const vec3& lightDirection, float sunAngularDiameter, const Camera& camera, float aspect,
				   const ShadowSettings& settings) {
	using namespace shadow;

	const float nearPlane = camera.nearPlane;
	const float farPlane  = std::max(std::min(settings.distance, camera.farPlane), nearPlane * 2.0f);

	// Practical split scheme: blend of uniform and logarithmic splits.
	float splits[kCascadeCount + 1];
	splits[0] = nearPlane;
	for (uint32_t i = 1; i <= kCascadeCount; ++i) {
		const float t			= static_cast<float>(i) / kCascadeCount;
		const float uniform		= nearPlane + (farPlane - nearPlane) * t;
		const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, t);
		splits[i]				= settings.cascadeSplitLambda * logarithmic + (1.0f - settings.cascadeSplitLambda) * uniform;
	}

	// Light basis shared by every cascade (used for texel snapping).
	const vec3 forward = lightDirection;
	const vec3 side	   = vec3::normalize(vec3::cross(forward, stableUp(forward)));
	const vec3 up	   = vec3::cross(side, forward);

	for (uint32_t i = 0; i < kCascadeCount; ++i) {
		vec3  center;
		float radius;
		frustumSliceSphere(camera, aspect, splits[i], splits[i + 1], center, radius);

		// Snap the center to whole shadow texels in light space: no shimmering when the camera moves.
		const float texelWorld = 2.0f * radius / kCascadeResolution;
		const float x		   = std::floor(vec3::dot(center, side) / texelWorld) * texelWorld;
		const float y		   = std::floor(vec3::dot(center, up) / texelWorld) * texelWorld;
		center				   = side * x + up * y + forward * vec3::dot(center, forward);

		const vec3	eye		   = center - forward * (radius + kCasterMargin);
		const float depthRange = 2.0f * radius + kCasterMargin;

		GpuShadowView& view = frame.shadowViews[kCascadeViewBase + i];
		writeView(view, cu::math::lookAt(eye, center, stableUp(forward)), orthographic(radius, 0.0f, depthRange), i, kCascadeColumns, kCascadeRows);
		view.params = {texelWorld, depthRange, 1.0f / (2.0f * radius), 1.0f / kCascadeResolution};

		frame.uniform.cascadeSplits[i] = splits[i + 1];
	}

	frame.uniform.shadowParams[0] = std::tan(std::max(sunAngularDiameter, 0.0f) * 0.5f * 3.14159265f / 180.0f);
	frame.uniform.shadowParams[1] = settings.cascadeBlend;
	frame.uniform.counts[1]		  = kCascadeCount;
	frame.cascadeViews			  = kCascadeCount;
}

void writePointShadow(LightFrame& frame, uint32_t slot, const vec3& position, float intensity, float maxRange) {
	using namespace shadow;

	// Distance where the light's contribution drops below ~1/255 (see pointAttenuation in lighting.frag).
	const float range = std::clamp(std::sqrt(std::max(intensity, 0.0f) * 255.0f / 0.032f), 1.0f, maxRange);

	static const vec3 kFaceDirections[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	const float		  tanHalfFov		 = 1.0f + 2.0f * kCubeGuardTexels / kPointFaceResolution;
	const mat4		  proj				 = perspectiveZeroToOne(tanHalfFov, kPointNear, range);

	for (uint32_t face = 0; face < 6; ++face) {
		const vec3	   direction = kFaceDirections[face];
		const uint32_t tile		 = slot * 6 + face;

		GpuShadowView& view = frame.shadowViews[kPointViewBase + tile];
		writeView(view, cu::math::lookAt(position, position + direction, stableUp(direction)), proj, tile, 6, kMaxPointShadows);
		view.params = {2.0f * tanHalfFov / kPointFaceResolution, range - kPointNear, 0.0f, 1.0f / kPointFaceResolution};
	}
	frame.pointViews = (slot + 1) * 6;
}

}

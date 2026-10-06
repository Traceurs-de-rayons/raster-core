#include "internal.hpp"

#include "Helpers.hpp"
#include "math/transform.hpp"

#include <algorithm>
#include <cmath>

namespace RasterCore::internal {

using cu::math::vec3;

void writeCameraUniform(CameraUniform& out, const Camera& camera, uint32_t width, uint32_t height) {
	const float aspect = static_cast<float>(width) / static_cast<float>(height);
	const float fovRad = camera.verticalFovDegrees * 3.14159265f / 180.0f;

	out.model = toFloatArray(cu::math::mat4::identity());
	out.view  = toFloatArray(cu::math::lookAt(camera.position, camera.target, camera.up));
	out.proj  = toFloatArray(cu::math::perspective(fovRad, aspect, camera.nearPlane, camera.farPlane));
}

namespace {

	vec3 safeNormalize(const vec3& v, const vec3& fallback) {
		const float length = v.length();
		return length > 1e-6f ? v * (1.0f / length) : fallback;
	}

	// Same falloff as pointAttenuation() in lighting.frag.
	float pointAttenuation(float distance) {
		return 1.0f / (1.0f + 0.09f * distance + 0.032f * distance * distance);
	}

	// Point and spot lights that get a cube shadow: the brightest ones as seen from the camera.
	std::vector<size_t> selectPointShadows(const std::vector<Light>& lights, size_t count, const Camera& camera) {
		std::vector<size_t> candidates;
		for (size_t i = 0; i < count; ++i) {
			if (lights[i].type != LightType::Directional && lights[i].castsShadow && lights[i].intensity > 0.0f)
				candidates.push_back(i);
		}

		auto score = [&](size_t i) { return lights[i].intensity * pointAttenuation((lights[i].position - camera.position).length()); };
		std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return score(a) > score(b); });
		if (candidates.size() > shadow::kMaxPointShadows)
			candidates.resize(shadow::kMaxPointShadows);
		return candidates;
	}

}

void buildLightFrame(LightFrame& out, const std::vector<Light>& lights, const Camera& camera, float aspect, const ShadowSettings& settings) {
	const vec3	 down{0.0f, -1.0f, 0.0f};
	const size_t count = std::min<size_t>(lights.size(), kMaxLights);

	out.uniform = {};
	out.lights.resize(count);
	out.shadowViews.assign(shadow::kMaxShadowViews, GpuShadowView{});
	out.cascadeViews = 0;
	out.pointViews	 = 0;

	const vec3 cameraForward	= safeNormalize(camera.target - camera.position, {0.0f, 0.0f, -1.0f});
	out.uniform.cameraPosition = {camera.position.x, camera.position.y, camera.position.z, 0.0f};
	out.uniform.cameraForward  = {cameraForward.x, cameraForward.y, cameraForward.z, 0.0f};
	out.uniform.counts[0]	   = static_cast<uint32_t>(count);

	for (size_t i = 0; i < count; ++i) {
		const Light& light	   = lights[i];
		const bool	 point	   = light.type != LightType::Directional; // has a position
		const vec3	 direction = safeNormalize(light.direction, down);
		const float	 type	   = light.type == LightType::Spot ? 2.0f : point ? 1.0f : 0.0f;

		out.lights[i].positionType		 = {light.position.x, light.position.y, light.position.z, type};
		out.lights[i].directionIntensity = {direction.x, direction.y, direction.z, light.intensity};
		out.lights[i].colorShadow		 = {light.color.x, light.color.y, light.color.z, -1.0f};

		if (light.type == LightType::Spot) {
			const float halfAngle = std::clamp(light.coneAngle, 1.0f, 179.0f) * 0.5f * 3.14159265f / 180.0f;
			const float cosOuter  = std::cos(halfAngle);
			const float cosInner  = std::cos(halfAngle * (1.0f - std::clamp(light.coneBlend, 0.0f, 1.0f)));
			out.lights[i].cone	  = {cosOuter, std::max(cosInner, cosOuter + 1e-4f), 0.0f, 0.0f};
		}

		// Cascaded shadows for the first shadow-casting directional light (suns come first).
		if (!point && light.castsShadow && out.cascadeViews == 0) {
			writeCascades(out, direction, light.angularDiameter, camera, aspect, settings);
			out.lights[i].colorShadow[3] = static_cast<float>(shadow::kCascadeViewBase);
		}
	}

	const std::vector<size_t> pointShadows = selectPointShadows(lights, count, camera);
	for (uint32_t slot = 0; slot < pointShadows.size(); ++slot) {
		const Light& light = lights[pointShadows[slot]];
		writePointShadow(out, slot, light.position, light.intensity, settings.maxPointShadowRange);
		out.lights[pointShadows[slot]].colorShadow[3] = static_cast<float>(shadow::kPointViewBase + slot * 6);
	}
}

}

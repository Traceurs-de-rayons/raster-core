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

	const float viewModelFov = camera.viewModelFovDegrees > 0.0f ? camera.viewModelFovDegrees * 3.14159265f / 180.0f : fovRad;

	out.viewModelProj = toFloatArray(cu::math::perspective(viewModelFov, aspect, camera.nearPlane, camera.farPlane));
	out.view		  = toFloatArray(cu::math::lookAt(camera.position, camera.target, camera.up));
	out.proj		  = toFloatArray(cu::math::perspective(fovRad, aspect, camera.nearPlane, camera.farPlane));
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

	// True when a sphere touches the camera frustum.
	bool sphereInFrustum(const Camera& camera, float aspect, const vec3& center, float radius) {
		const vec3	forward = safeNormalize(camera.target - camera.position, {0.0f, 0.0f, -1.0f});
		const vec3	right	= safeNormalize(vec3::cross(forward, camera.up), {1.0f, 0.0f, 0.0f});
		const vec3	up		= vec3::cross(right, forward);
		const float tanV	= std::tan(camera.verticalFovDegrees * 0.5f * 3.14159265f / 180.0f);
		const float tanH	= tanV * aspect;

		const vec3	toCenter = center - camera.position;
		const float depth	 = vec3::dot(toCenter, forward);
		if (depth < camera.nearPlane - radius || depth > camera.farPlane + radius)
			return false;

		// Signed distance to each side plane (positive inside), from its inward normal.
		const float side	   = vec3::dot(toCenter, right);
		const float height	   = vec3::dot(toCenter, up);
		const float horizontal = 1.0f / std::sqrt(1.0f + tanH * tanH);
		const float vertical   = 1.0f / std::sqrt(1.0f + tanV * tanV);
		return (depth * tanH - side) * horizontal >= -radius && (depth * tanH + side) * horizontal >= -radius &&
			   (depth * tanV - height) * vertical >= -radius && (depth * tanV + height) * vertical >= -radius;
	}

	struct ShadowCandidate {
		size_t light;
		float  score;
		bool   visible; // its light can reach something on screen
	};

	// Gives the point and spot lights their shadow slots: the ones that reach the camera frustum
	// first, then the brightest as seen from the camera. A light keeps the slot it already has.
	std::vector<ShadowCandidate> assignShadowSlots(const std::vector<Light>& lights, size_t count, const Camera& camera, float aspect,
												   const ShadowSettings& settings, ShadowSlots& slots) {
		std::vector<ShadowCandidate> candidates;
		for (size_t i = 0; i < count; ++i) {
			const Light& light = lights[i];
			if (light.type == LightType::Directional || !light.castsShadow || light.intensity <= 0.0f)
				continue;
			const float range = pointLightRange(light.intensity, settings.maxPointShadowRange);
			candidates.push_back({i, light.intensity * pointAttenuation((light.position - camera.position).length()),
								  sphereInFrustum(camera, aspect, light.position, range)});
		}

		std::stable_sort(candidates.begin(), candidates.end(), [](const ShadowCandidate& a, const ShadowCandidate& b) {
			return a.visible != b.visible ? a.visible : a.score > b.score;
		});
		if (candidates.size() > shadow::kMaxPointShadows)
			candidates.resize(shadow::kMaxPointShadows);

		auto selected = [&](int light) {
			return std::any_of(candidates.begin(), candidates.end(), [&](const ShadowCandidate& c) { return static_cast<int>(c.light) == light; });
		};
		for (int& owner : slots) {
			if (owner >= 0 && !selected(owner))
				owner = -1;
		}
		for (const ShadowCandidate& candidate : candidates) {
			if (std::find(slots.begin(), slots.end(), static_cast<int>(candidate.light)) != slots.end())
				continue;
			*std::find(slots.begin(), slots.end(), -1) = static_cast<int>(candidate.light);
		}
		return candidates;
	}

}

void buildLightFrame(LightFrame& out, const std::vector<Light>& lights, const Camera& camera, float aspect, const ShadowSettings& settings,
					 ShadowSlots& slots) {
	const vec3	 down{0.0f, -1.0f, 0.0f};
	const size_t count = std::min<size_t>(lights.size(), kMaxLights);

	out.uniform = {};
	out.lights.resize(count);
	out.shadowViews.assign(shadow::kMaxShadowViews, GpuShadowView{});
	out.cascadeTiles	  = 0;
	out.pointTiles		  = 0;
	out.pointVisibleTiles = 0;

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
		if (!point && light.castsShadow && out.cascadeTiles == 0) {
			writeCascades(out, direction, light.angularDiameter, camera, aspect, settings);
			out.lights[i].colorShadow[3] = static_cast<float>(shadow::kCascadeViewBase);
		}
	}

	const std::vector<ShadowCandidate> pointShadows = assignShadowSlots(lights, count, camera, aspect, settings, slots);
	for (const ShadowCandidate& candidate : pointShadows) {
		const Light&   light = lights[candidate.light];
		const uint32_t slot	 = static_cast<uint32_t>(std::find(slots.begin(), slots.end(), static_cast<int>(candidate.light)) - slots.begin());

		writePointShadow(out, slot, light.position, light.intensity, settings.maxPointShadowRange);
		out.lights[candidate.light].colorShadow[3] = static_cast<float>(shadow::kPointViewBase + slot * 6);
		if (candidate.visible)
			out.pointVisibleTiles |= 0x3full << (slot * 6);
	}
}

}

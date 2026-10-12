#pragma once

#include <cstdint>

// Layout of the shadow atlases and of the shadow view buffer, shared by the render graph
// config, the pipeline and the shaders (see lighting.frag.slang and shadow.vert.slang).

namespace RasterCore::shadow {

// Sun: one atlas tile per cascade. Must match kCascadeCount in lighting.frag.slang.
inline constexpr uint32_t kCascadeCount		 = 4;
inline constexpr uint32_t kCascadeColumns	 = 2;
inline constexpr uint32_t kCascadeRows		 = 2;
inline constexpr uint32_t kCascadeResolution = 2048; // pixels per cascade tile

// Point and spot lights: 6 cube faces per light, one light per atlas row.
inline constexpr uint32_t kMaxPointShadows	   = 4;
inline constexpr uint32_t kPointFaceResolution = 1024; // pixels per cube face

// Shadow view buffer: the cascades first (the shaders index them directly), then the cube faces.
inline constexpr uint32_t kCascadeViewBase = 0;
inline constexpr uint32_t kPointViewBase   = kCascadeViewBase + kCascadeColumns * kCascadeRows;
inline constexpr uint32_t kMaxShadowViews  = kPointViewBase + kMaxPointShadows * 6;

// Rasterizer depth bias of the shadow passes.
inline constexpr float kDepthBiasConstant = 2.0f;
inline constexpr float kDepthBiasSlope	  = 2.0f;

static_assert(kCascadeCount <= kCascadeColumns * kCascadeRows, "one atlas tile per cascade");
static_assert(kCascadeCount <= 4, "LightUniform::cascadeSplits holds 4 cascades");
// Tiles are tracked as one bit each in a uint64_t (see LightFrame).
static_assert(kMaxPointShadows * 6 <= 64, "too many point shadow tiles for a 64-bit mask");

}

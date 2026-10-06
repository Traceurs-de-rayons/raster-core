// Deferred lighting: every light of the scene, with
//  - sun / main directional light: cascaded shadow maps + PCSS soft shadows (contact hardening)
//  - point and spot lights: cube shadow maps (6 atlas tiles per light) + rotated Poisson PCF
// Layouts mirror RasterCore::internal (pipeline/internal.hpp) and shadow::* (core/ShadowLayout.hpp).

struct FragmentOutput {
  float4 color : SV_Target0;
};

struct LightUBO {
  float4 cameraPosition;
  float4 cameraForward;
  float4 cascadeSplits; // view depth at which each cascade ends
  float4 shadowParams;  // x = tan(sun half angle), y = cascade blend fraction
  uint4 counts;         // x = lights, y = active cascades (0 = no sun shadow)
};

[[vk::binding(0, 0)]] Sampler2D gAlbedo;
[[vk::binding(1, 0)]] Sampler2D gNormal;
[[vk::binding(2, 0)]] Sampler2D gWorldPos;
[[vk::binding(3, 0)]] Sampler2DShadow sunShadow;    // comparison sampler (hardware PCF)
[[vk::binding(4, 0)]] Sampler2D sunShadowDepth;     // raw depth, nearest (blocker search)
[[vk::binding(5, 0)]] Sampler2DShadow pointShadow;  // comparison sampler (hardware PCF)
[[vk::binding(21, 0)]] ConstantBuffer<LightUBO> lighting; // RasterCore::binding::Light
// Raw float4 arrays: radv crashes compiling Slang's StructuredBuffer<struct>.
[[vk::binding(22, 0)]] StructuredBuffer<float4> lightData;   // 4 float4 per light
[[vk::binding(23, 0)]] StructuredBuffer<float4> shadowViews; // 10 float4 per view

static const float kAmbient = 0.15;
static const uint kCascadeCount = 4;

static const float2 kPoisson[16] = {
  float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
  float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
  float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
  float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
  float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
  float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
  float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
  float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

struct GpuLight {
  float3 position;
  bool isPoint; // point or spot: has a position
  bool isSpot;
  float3 direction; // direction the light travels
  float intensity;
  float3 color;
  int shadowView; // first shadow view index, -1 = no shadow
  float2 cone;    // spot: x = cos(outer half angle), y = cos(inner half angle)
};

GpuLight loadLight(uint index) {
  float4 positionType = lightData[index * 4 + 0];
  float4 directionIntensity = lightData[index * 4 + 1];
  float4 colorShadow = lightData[index * 4 + 2];

  GpuLight light;
  light.position = positionType.xyz;
  light.isPoint = positionType.w > 0.5;
  light.isSpot = positionType.w > 1.5;
  light.direction = directionIntensity.xyz;
  light.intensity = directionIntensity.w;
  light.color = colorShadow.rgb;
  light.shadowView = int(colorShadow.w);
  light.cone = lightData[index * 4 + 3].xy;
  return light;
}

struct ShadowView {
  uint base;
  float4 atlasRect; // xy = tile offset, zw = tile size (atlas UV)
  float4 params;    // cascade: x = texel world size, y = depth range, z = tile UV per world unit; w = 1 / tile resolution
};

ShadowView loadShadowView(uint index) {
  ShadowView view;
  view.base = index * 10;
  view.atlasRect = shadowViews[view.base + 8];
  view.params = shadowViews[view.base + 9];
  return view;
}

// Matrices are stored column by column (cu::math layout).
float4 transformByColumns(uint base, float4 v) {
  return shadowViews[base + 0] * v.x + shadowViews[base + 1] * v.y + shadowViews[base + 2] * v.z +
         shadowViews[base + 3] * v.w;
}

// World position -> (tile UV, depth) of a shadow view.
float3 projectToShadow(ShadowView view, float3 worldPos) {
  float4 clip = transformByColumns(view.base + 4, transformByColumns(view.base, float4(worldPos, 1.0)));
  float3 ndc = clip.xyz / clip.w;
  return float3(ndc.xy * 0.5 + 0.5, ndc.z);
}

float2 tileToAtlas(ShadowView view, float2 tileUv) {
  // Stay half a texel inside the tile so filtering never reads a neighbour tile.
  float halfTexel = 0.5 * view.params.w;
  return view.atlasRect.xy + clamp(tileUv, halfTexel, 1.0 - halfTexel) * view.atlasRect.zw;
}

// Per-pixel random rotation of the Poisson disk (interleaved gradient noise).
float2x2 randomRotation(float2 pixel) {
  float angle = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
  float s = sin(angle), c = cos(angle);
  return float2x2(c, -s, s, c);
}

// ---------------------------------------------------------------------------
// Sun: cascaded shadow maps + PCSS
// ---------------------------------------------------------------------------

float sunCascadeShadow(uint cascade, float3 worldPos, float3 normal, float ndl, float2x2 rotation) {
  ShadowView view = loadShadowView(cascade);
  float texelWorld = view.params.x;
  float depthRange = view.params.y;
  float uvPerWorld = view.params.z;
  float texelUv = view.params.w;

  // Normal offset: push the lookup along the normal, more at grazing angles.
  float3 offsetPos = worldPos + normal * texelWorld * (0.5 + 1.5 * (1.0 - ndl));
  float3 shadowPos = projectToShadow(view, offsetPos);
  if (any(shadowPos.xy < 0.0) || any(shadowPos.xy > 1.0) || shadowPos.z > 1.0)
    return 1.0;

  float receiver = shadowPos.z;
  float lightSize = 2.0 * lighting.shadowParams.x; // penumbra width per world unit of occluder distance

  // Receiver-plane depth bias: depth of the receiver's own plane at a tile-UV offset, so the
  // wide PCSS kernels do not treat a surface sloped relative to the light as its own occluder.
  float3 axisX = float3(shadowViews[view.base + 0].x, shadowViews[view.base + 1].x, shadowViews[view.base + 2].x);
  float3 axisY = float3(shadowViews[view.base + 0].y, shadowViews[view.base + 1].y, shadowViews[view.base + 2].y);
  float3 lightDir = -float3(shadowViews[view.base + 0].z, shadowViews[view.base + 1].z, shadowViews[view.base + 2].z);
  float facing = max(-dot(normal, lightDir), 0.2); // limits the slope at grazing angles
  float2 depthGradient = float2(dot(normal, axisX), dot(normal, axisY)) / (facing * uvPerWorld * depthRange);

  // 1. Blocker search: average depth of the occluders around the pixel.
  float searchRadius = clamp(lightSize * 20.0 * uvPerWorld, 2.0 * texelUv, 48.0 * texelUv);
  float blockerSum = 0.0;
  float blockers = 0.0;
  for (uint i = 0; i < 16; ++i) {
    float2 offset = mul(rotation, kPoisson[i]) * searchRadius;
    float depth = sunShadowDepth.SampleLevel(tileToAtlas(view, shadowPos.xy + offset), 0.0).r;
    if (depth < receiver + dot(depthGradient, offset) - 0.0005) {
      blockerSum += depth;
      blockers += 1.0;
    }
  }
  if (blockers == 0.0)
    return 1.0;

  // 2. Penumbra from the occluder distance and the sun's angular size.
  float occluderDistance = (receiver - blockerSum / blockers) * depthRange;
  float filterRadius = clamp(occluderDistance * lightSize * uvPerWorld, 1.0 * texelUv, 48.0 * texelUv);

  // 3. Filter: rotated Poisson disk of hardware-PCF taps.
  float lit = 0.0;
  for (uint i = 0; i < 16; ++i) {
    float2 offset = mul(rotation, kPoisson[i]) * filterRadius;
    lit += sunShadow.SampleCmpLevelZero(tileToAtlas(view, shadowPos.xy + offset), receiver + dot(depthGradient, offset));
  }
  return lit / 16.0;
}

float sunShadowFactor(float3 worldPos, float3 normal, float ndl, float2x2 rotation) {
  uint cascades = lighting.counts.y;
  if (cascades == 0)
    return 1.0;

  float viewDepth = dot(worldPos - lighting.cameraPosition.xyz, lighting.cameraForward.xyz);
  uint cascade = 0;
  while (cascade < cascades && viewDepth > lighting.cascadeSplits[cascade])
    ++cascade;
  if (cascade >= cascades)
    return 1.0; // beyond the shadow distance

  float shadow = sunCascadeShadow(cascade, worldPos, normal, ndl, rotation);

  // Cross-fade into the next cascade (or out of shadows after the last one).
  float start = cascade == 0 ? 0.0 : lighting.cascadeSplits[cascade - 1];
  float end = lighting.cascadeSplits[cascade];
  float blend = saturate((viewDepth - (end - (end - start) * lighting.shadowParams.y)) / max((end - start) * lighting.shadowParams.y, 1e-4));
  if (blend > 0.0) {
    float next = cascade + 1 < cascades ? sunCascadeShadow(cascade + 1, worldPos, normal, ndl, rotation) : 1.0;
    shadow = lerp(shadow, next, blend);
  }
  return shadow;
}

// ---------------------------------------------------------------------------
// Point lights: cube shadow maps
// ---------------------------------------------------------------------------

uint cubeFace(float3 v) {
  float3 a = abs(v);
  if (a.x >= a.y && a.x >= a.z)
    return v.x > 0.0 ? 0 : 1;
  if (a.y >= a.z)
    return v.y > 0.0 ? 2 : 3;
  return v.z > 0.0 ? 4 : 5;
}

float pointShadowFactor(GpuLight light, float3 worldPos, float3 normal, float ndl, float2x2 rotation) {
  float3 fromLight = worldPos - light.position;
  float distance = length(fromLight);
  ShadowView view = loadShadowView(uint(light.shadowView) + cubeFace(fromLight));

  float texelWorld = view.params.x * distance;
  float3 offsetPos = worldPos + normal * texelWorld * (0.5 + 1.5 * (1.0 - ndl));
  float3 shadowPos = projectToShadow(view, offsetPos);
  if (shadowPos.z > 1.0)
    return 1.0; // beyond the shadow range

  float radius = 1.5 * view.params.w;
  float lit = 0.0;
  for (uint i = 0; i < 8; ++i) {
    float2 uv = tileToAtlas(view, shadowPos.xy + mul(rotation, kPoisson[i * 2]) * radius);
    lit += pointShadow.SampleCmpLevelZero(uv, shadowPos.z);
  }
  return lit / 8.0;
}

// ---------------------------------------------------------------------------

// Smooth inverse-square falloff (constant/linear/quadratic terms), mirrored in Uniforms.cpp.
float pointAttenuation(float distance) {
  return 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
}

[shader("fragment")]
FragmentOutput main(float4 fragCoord : SV_Position, float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  float3 albedo = gAlbedo.Sample(texCoord).rgb;
  float3 encodedNormal = gNormal.Sample(texCoord).xyz;
  if (dot(encodedNormal, encodedNormal) < 1e-4) {
    output.color = float4(albedo, 1.0); // background: nothing was drawn here
    return output;
  }

  float3 normal = normalize(encodedNormal * 2.0 - 1.0);
  float3 worldPos = gWorldPos.Sample(texCoord).xyz;
  float2x2 rotation = randomRotation(fragCoord.xy);

  float3 lit = float3(0.0);
  for (uint i = 0; i < lighting.counts.x; ++i) {
    GpuLight light = loadLight(i);

    float3 toLight = -light.direction;
    float attenuation = 1.0;
    if (light.isPoint) {
      float3 delta = light.position - worldPos;
      float distance = length(delta);
      toLight = delta / max(distance, 0.0001);
      attenuation = pointAttenuation(distance);
      if (light.isSpot)
        attenuation *= smoothstep(light.cone.x, light.cone.y, dot(-toLight, light.direction));
    }

    float ndl = max(dot(normal, toLight), 0.0);
    if (ndl <= 0.0 || attenuation * light.intensity <= 0.0)
      continue;

    float shadow = 1.0;
    if (light.shadowView >= 0)
      shadow = light.isPoint ? pointShadowFactor(light, worldPos, normal, ndl, rotation)
                             : sunShadowFactor(worldPos, normal, ndl, rotation);

    lit += light.color * light.intensity * attenuation * ndl * shadow;
  }

  output.color = float4(albedo * (lit + kAmbient), 1.0);
  return output;
}

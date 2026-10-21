// Toon lighting for the "training simulation" look: flat shading in a few bands, a world-space
// grid on the neutral surfaces, unlit emissive surfaces and fog. Writes HDR: emissive surfaces
// go above 1 and feed the bloom.
// The material of the surface (occlusion, roughness, metallic, emission) comes from the G-buffer;
// the highlight of a light is as sharp as its roughness says, and the occlusion darkens what the
// ambient gives.

struct FragmentOutput {
  float4 color : SV_Target0;
};

#include "lighting_common.slang"

// Soft steps instead of a smooth gradient: 0, 1/bands, ..., 1.
float toonRamp(float value, float bands) {
  float scaled = saturate(value) * bands;
  return (floor(scaled) + smoothstep(0.42, 0.58, frac(scaled))) / bands;
}

// 1 on the lines of a grid of the given period, antialiased with the pixel footprint.
float gridLines(float2 position, float period, float width, float footprint) {
  float2 distance = abs(frac(position / period - 0.5) - 0.5) * period;
  float nearest = min(distance.x, distance.y);
  float line = 1.0 - smoothstep(width, width + footprint, nearest);
  return line * saturate(1.0 - footprint / (period * 0.3)); // fades before it turns into noise
}

[shader("fragment")]
FragmentOutput main(float4 fragCoord : SV_Position, float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  float4 surface = gAlbedo.Sample(texCoord);
  float3 albedo = surface.rgb;
  float3 encodedNormal = gNormal.Sample(texCoord).xyz;
  if (dot(encodedNormal, encodedNormal) < 1e-4) {
    output.color = float4(albedo, 1.0); // background: nothing was drawn here
    return output;
  }

  float3 normal = normalize(encodedNormal * 2.0 - 1.0);
  float3 worldPos = gWorldPos.Sample(texCoord).xyz;
  float2x2 rotation = randomRotation(fragCoord.xy);
  SurfaceMaterial material = loadMaterial(texCoord);

  // The normal of the surface itself, from the screen derivatives of where it is: the grid and
  // the shadows need the surface, not the detail a normal map adds to it (with the map, the grid
  // would land on the wrong plane and the surface would shadow itself).
  float3 surfaceNormal = normalize(cross(ddy(worldPos), ddx(worldPos)));
  if (dot(surfaceNormal, normal) < 0.0)
    surfaceNormal = -surfaceNormal;

  float bands = max(lighting.effects[2].z, 1.0);
  float ambient = lighting.effects[3].w;

  float3 toView = normalize(lighting.cameraPosition.xyz - worldPos);
  // What the roughness says about the highlight: a mirror stays a point, a matte surface spreads.
  float gloss = 1.0 - material.roughness;
  float highlightPower = exp2(2.0 + 9.0 * gloss);
  float3 specularColor = lerp(float3(0.04, 0.04, 0.04), albedo, material.metallic);

  float3 direct = float3(0.0);
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

    // Only the shadowed lights are banded: their penumbra becomes a few clean steps.
    float shadow = 1.0;
    float amount = ndl;
    if (light.shadowView >= 0) {
      shadow = light.isPoint ? pointShadowFactor(light, worldPos, surfaceNormal, ndl, rotation)
                             : sunShadowFactor(worldPos, surfaceNormal, ndl, rotation);
      amount = ndl * toonRamp(shadow, bands);
    }

    // A metal has no diffuse: what it sends back is the highlight, tinted by its color. A matte
    // surface shows almost none of it, which keeps the flat look of the game on the walls.
    float3 halfway = normalize(toLight + toView);
    float highlight = pow(saturate(dot(normal, halfway)), highlightPower) * (highlightPower + 2.0) / 8.0;
    float3 diffuse = albedo * (1.0 - material.metallic) * amount;
    direct += light.color * light.intensity * attenuation * (diffuse + specularColor * highlight * (1.0 - material.roughness) * shadow);
  }

  // A little more ambient from above than from below, and the occlusion takes its share. A metal
  // has nothing diffuse: what it shows of the sky is its own color, as sharp as its roughness.
  float3 ambientDiffuse = albedo * ambient * (0.8 + 0.2 * normal.y) * (1.0 - material.metallic);
  float3 ambientSpecular = albedo * ambient * (1.0 - material.roughness) * material.metallic;
  float3 color = direct + (ambientDiffuse + ambientSpecular) * material.occlusion;

  // Grid on the grey surfaces only: colored ones (players, accents) stay clean.
  float highest = max(albedo.r, max(albedo.g, albedo.b));
  float lowest = min(albedo.r, min(albedo.g, albedo.b));
  float saturation = highest > 0.0 ? (highest - lowest) / highest : 0.0;
  float gridStrength = lighting.effects[2].y * (1.0 - smoothstep(0.03, 0.12, saturation));
  if (gridStrength > 0.0) {
    float3 facing = abs(surfaceNormal);
    float2 plane = facing.y > 0.5 ? worldPos.xz : (facing.x > 0.5 ? worldPos.zy : worldPos.xy);
    float footprint = max(fwidth(plane.x), fwidth(plane.y));
    float minor = gridLines(plane, 1.0, 0.012, footprint);
    float major = gridLines(plane, 4.0, 0.03, footprint);
    color *= 1.0 - gridStrength * material.occlusion * max(minor * 0.16, major * 0.34);
  }

  float emission = material.emission;
  color = lerp(color, albedo * lighting.effects[2].w, emission);

  // Fog: thicker with the distance and near the ground.
  float3 fromCamera = worldPos - lighting.cameraPosition.xyz;
  float fog = 1.0 - exp(-length(fromCamera) * lighting.effects[1].w);
  fog *= exp(-max(worldPos.y - lighting.effects[2].x, 0.0) * 0.06);
  color = lerp(color, lighting.effects[1].rgb, saturate(fog) * (1.0 - 0.5 * emission));

  output.color = float4(color, 1.0);
  return output;
}

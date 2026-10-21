// Deferred lighting: every light of the scene, with soft shadows (see lighting_common.slang).

struct FragmentOutput {
  float4 color : SV_Target0;
};

#include "lighting_common.slang"

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
  SurfaceMaterial material = loadMaterial(texCoord);

  float3 toView = normalize(lighting.cameraPosition.xyz - worldPos);
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

    float shadow = 1.0;
    if (light.shadowView >= 0)
      shadow = light.isPoint ? pointShadowFactor(light, worldPos, normal, ndl, rotation)
                             : sunShadowFactor(worldPos, normal, ndl, rotation);

    // A metal has no diffuse: what it sends back is the highlight, tinted by its color.
    float3 halfway = normalize(toLight + toView);
    float highlight = pow(saturate(dot(normal, halfway)), highlightPower) * (highlightPower + 2.0) / 8.0;
    float3 diffuse = albedo * (1.0 - material.metallic) * ndl;
    direct += light.color * light.intensity * attenuation * shadow * (diffuse + specularColor * highlight * (1.0 - material.roughness));
  }

  // A metal has nothing diffuse: what it shows of the sky is its own color, as sharp as its
  // roughness (there is no environment map to reflect).
  float3 ambientDiffuse = albedo * kAmbient * (1.0 - material.metallic);
  float3 ambientSpecular = albedo * kAmbient * (1.0 - material.roughness) * material.metallic;
  output.color = float4(direct + (ambientDiffuse + ambientSpecular) * material.occlusion, 1.0);
  return output;
}

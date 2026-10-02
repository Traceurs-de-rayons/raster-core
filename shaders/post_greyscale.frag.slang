struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]]
Sampler2D sceneTexture;

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  float3 c = sceneTexture.Sample(texCoord).rgb;
  float luminance = dot(c, float3(0.299, 0.587, 0.114));
  output.color = float4(luminance, luminance, luminance, 1.0);

  return output;
}
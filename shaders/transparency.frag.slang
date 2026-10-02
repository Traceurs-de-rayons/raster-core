struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]]
Sampler2D sceneTexture;

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;
  output.color = sceneTexture.Sample(texCoord);
  return output;
}
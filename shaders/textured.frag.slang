// Fragment shader in Slang

struct VertexOutput {
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 texCoord : TEXCOORD;
  uint textureId : TEXTUREID;
};

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("fragment")]
FragmentOutput main(VertexOutput input) {
  FragmentOutput output;

  float4 texColor =
      textures[NonUniformResourceIndex(input.textureId)].Sample(input.texCoord);
  output.color = texColor;

  return output;
}

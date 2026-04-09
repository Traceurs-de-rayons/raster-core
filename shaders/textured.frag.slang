struct VertexOutput {
  float4 position : SV_Position;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
};

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("fragment")]
FragmentOutput main(VertexOutput input) {
  FragmentOutput output;

  float4 texColor = float4(1.0, 0.0, 0.0, 1.0);
  output.color = texColor;

  return output;
}

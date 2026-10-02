struct VertexOutput {
  float4 position : SV_Position;
  float2 texCoord : TEXCOORD;
};

[shader("vertex")]
VertexOutput main(uint vertexId : SV_VertexID) {
  VertexOutput output;

  float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
  output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
  output.texCoord = uv;

  return output;
}
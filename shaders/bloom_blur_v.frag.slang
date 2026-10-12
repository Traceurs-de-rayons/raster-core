// Bloom, step 2: vertical half of a separable gaussian blur (9 pixels wide, 5 bilinear taps).

struct FragmentOutput {
  float4 color : SV_Target0;
};

[[vk::binding(0, 0)]] Sampler2D source;

[shader("fragment")]
FragmentOutput main(float2 texCoord : TEXCOORD) {
  FragmentOutput output;

  uint width, height;
  source.GetDimensions(width, height);
  float2 step = float2(0.0, 1.0) / float2(width, height);

  float3 sum = source.Sample(texCoord).rgb * 0.2270270270;
  sum += (source.Sample(texCoord + step * 1.3846153846).rgb + source.Sample(texCoord - step * 1.3846153846).rgb) * 0.3162162162;
  sum += (source.Sample(texCoord + step * 3.2307692308).rgb + source.Sample(texCoord - step * 3.2307692308).rgb) * 0.0702702703;

  output.color = float4(sum, 1.0);
  return output;
}

struct FragmentOutput {
  float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main() {
  FragmentOutput output;
  output.color = float4(1, 1, 0, 1);
  return output;
}

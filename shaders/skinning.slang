// Skinning shared by the vertex shaders of the scene passes.
// Bone matrices are stored like the model matrices (row major, used as mul(M, v)).

[[vk::binding(24, 0)]] // RasterCore::binding::Bones
StructuredBuffer<float4x4> boneMatrices;

[[vk::binding(25, 0)]] // RasterCore::binding::DrawInfo
StructuredBuffer<uint> drawInfos;

static const uint kViewModelBit = 0x80000000u; // SharedGpuResources::kViewModelBit

bool isViewModel(uint drawId) {
  return (drawInfos[drawId] & kViewModelBit) != 0;
}

// Matrix moving a vertex of the draw from its bind pose to its current pose. Vertices whose
// weights are all zero are not skinned.
float4x4 skinMatrix(uint drawId, uint4 bones, float4 weights) {
  if (weights.x + weights.y + weights.z + weights.w <= 0.0) {
    return float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
  }
  uint first = drawInfos[drawId] & ~kViewModelBit;
  return boneMatrices[first + bones.x] * weights.x + boneMatrices[first + bones.y] * weights.y +
         boneMatrices[first + bones.z] * weights.z + boneMatrices[first + bones.w] * weights.w;
}

// The geometry pass: what a surface looks like (albedo, normal, occlusion / roughness / metallic)
// and where it is. Everything is deferred: the lighting passes read these four images.
//
// The materials are in the buffer of binding 19, one entry per material id, which the per-draw
// data (binding 18) gives. The layout mirrors SceneDescriptor's GpuMaterial: four rows of four
// 32-bit values, so both sides read the same bytes.

struct VertexOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD2;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  nointerpolation uint materialId : TEXCOORD1;
};

struct FragmentOutput {
  float4 albedo : SV_Target0;   // rgb = color, a = the alpha of the texture (a cutout reads it)
  float4 normal : SV_Target1;   // the normal of the surface, normal map included
  float4 worldPos : SV_Target2;
  float4 material : SV_Target3; // x = occlusion, y = roughness, z = metallic, w = emission
};

// The material as the buffer holds it: four rows of four 32-bit values, the layout of
// SceneDescriptor's GpuMaterial. Read as raw float4 (radv crashes compiling Slang's
// StructuredBuffer<struct>, see lighting_common.slang), and put back together here.
struct GpuMaterial {
  float4 tint;      // rgb = albedo tint, a = tiles per world unit
  float4 params;    // x = roughness, y = metallic, z = occlusion, w = normal intensity
  float4 emission;  // rgb = color, a = strength (0 = plain surface, 1 = fully emissive)
  uint4 textures;   // x = albedo, y = normal, z = orm, w = flags
};

// Flags of GpuMaterial::textures.w
static const uint kHasNormal = 1;
static const uint kHasOrm = 2;
static const uint kCutout = 4;

[[vk::binding(0, 0)]]
Sampler2D textures[128];

[[vk::binding(19, 0)]]
StructuredBuffer<float4> materialData; // 4 float4 per material

GpuMaterial loadMaterial(uint index) {
  GpuMaterial material;
  material.tint = materialData[index * 4 + 0];
  material.params = materialData[index * 4 + 1];
  material.emission = materialData[index * 4 + 2];
  material.textures = asuint(materialData[index * 4 + 3]);
  return material;
}

// The tangent frame of a surface, built from what the screen gives us (the derivatives of the
// world position and of the UVs): no tangent has to be stored with the vertices. See Christian
// Schuler, "Normal Mapping Without Precomputed Tangents".
//
// Seen at a grazing angle the two UV derivatives become parallel and the frame has no meaning
// (it would turn the normal anywhere): there, the normal of the surface is kept as it is.
float3 applyNormalMap(float3 n, float2 uv, float3 worldPos, float3 sampled) {
  float3 dp1 = ddx(worldPos);
  float3 dp2 = ddy(worldPos);
  float2 duv1 = ddx(uv);
  float2 duv2 = ddy(uv);

  float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
  if (abs(determinant) < 1e-9)
    return n;

  float3 dp2perp = cross(dp2, n);
  float3 dp1perp = cross(n, dp1);
  float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
  float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = rsqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
  // Written out rather than as a matrix product: what a float3x3(a, b, c) means (rows or
  // columns) depends on the compiler's matrix layout, and a normal map turned the wrong way
  // reads as a surface lit from below.
  return normalize(tangent * (invmax * sampled.x) + bitangent * (invmax * sampled.y) + n * sampled.z);
}

[shader("fragment")]
FragmentOutput main(VertexOutput input) {
  FragmentOutput output;
  GpuMaterial material = loadMaterial(input.materialId);

  // The UVs of the map are in metres; a material says how many tiles that is.
  float2 uv = input.texCoord * material.tint.a;
  float4 albedo = textures[material.textures.x].Sample(uv) * float4(material.tint.rgb, 1.0);

  // Cutout: what the alpha hides is not drawn at all (grates, foliage, holes in a wall).
  if ((material.textures.w & kCutout) != 0 && albedo.a < 0.5)
    discard;

  float3 normal = normalize(input.normal);
  if ((material.textures.w & kHasNormal) != 0) {
    float3 sampled = textures[material.textures.y].Sample(uv).xyz * 2.0 - 1.0;
    sampled.xy *= material.params.w; // how deep the map is read
    normal = applyNormalMap(normal, uv, input.worldPos, sampled);
  }

  // Without an occlusion / roughness / metallic image, the fallback (white) leaves the numbers
  // of the material as they are.
  float4 orm = textures[material.textures.z].Sample(uv);
  float3 pbr = float3(orm.r * material.params.z, orm.g * material.params.x, orm.b * material.params.y);

  output.albedo = float4(albedo.rgb, albedo.a);
  output.normal = float4(normal * 0.5 + 0.5, 1.0);
  output.worldPos = float4(input.worldPos, 1.0);
  output.material = float4(saturate(pbr), material.emission.a);
  return output;
}

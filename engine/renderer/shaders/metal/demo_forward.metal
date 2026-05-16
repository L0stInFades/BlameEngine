#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
    float3 albedo [[attribute(3)]];
};

struct Uniforms {
    float4x4 mvp;
    float4x4 model;
    float4 lightDirection;
    float4 cameraPosition;
    float4 material;
    float4 ambientColor;
    float4 debugTint;
};

constant uint kMaterialTextureBaseColor = 0;
constant uint kMaterialTextureNormal = 1;
constant uint kMaterialTextureMetallicRoughness = 2;
constant uint kMaterialTextureEmissive = 3;
constant uint kMaterialTextureOcclusion = 4;
constant uint kMaterialTextureCount = 5;

struct MaterialShaderResourceGroup {
    constant Uniforms* uniforms [[id(0)]];
    array<texture2d<float>, kMaterialTextureCount> textures [[id(1)]];
    array<sampler, kMaterialTextureCount> samplers [[id(6)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 worldNormal;
    float2 texcoord;
    float3 albedo;
};

float3 ACESFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 ApplyNormalMap(float3 geometricNormal, float3 worldPosition, float2 texcoord, float3 sampledNormal) {
    const float3 normal = normalize(geometricNormal);
    const float3 tangentNormal = normalize(sampledNormal * 2.0 - 1.0);
    const float3 dp1 = dfdx(worldPosition);
    const float3 dp2 = dfdy(worldPosition);
    const float2 duv1 = dfdx(texcoord);
    const float2 duv2 = dfdy(texcoord);
    const float3 dp2perp = cross(dp2, normal);
    const float3 dp1perp = cross(normal, dp1);
    const float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    const float frameScale = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (frameScale <= 0.000001) {
        return normal;
    }
    const float invFrameScale = rsqrt(frameScale);
    const float3x3 tangentFrame = float3x3(tangent * invFrameScale, bitangent * invFrameScale, normal);
    return normalize(tangentFrame * tangentNormal);
}

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    const float4 worldPosition = uniforms.model * float4(in.position, 1.0);
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.worldPosition = worldPosition.xyz;
    out.worldNormal = normalize((uniforms.model * float4(in.normal, 0.0)).xyz);
    out.texcoord = in.texcoord;
    out.albedo = in.albedo;
    return out;
}

fragment float4 fragment_main_material_srg(VertexOut in [[stage_in]],
                                           constant MaterialShaderResourceGroup& materialSrg [[buffer(2)]]) {
    const constant Uniforms* fragmentUniforms = materialSrg.uniforms;
    const float3 normalSample = materialSrg.textures[kMaterialTextureNormal]
                                    .sample(materialSrg.samplers[kMaterialTextureNormal], in.texcoord)
                                    .rgb;
    const float3 normal = ApplyNormalMap(in.worldNormal, in.worldPosition, in.texcoord, normalSample);
    const float3 lightDir = normalize(-fragmentUniforms->lightDirection.xyz);
    const float3 viewDir = normalize(fragmentUniforms->cameraPosition.xyz - in.worldPosition);
    const float3 halfVec = normalize(lightDir + viewDir);

    const float4 sampledMetallicRoughness =
        materialSrg.textures[kMaterialTextureMetallicRoughness]
            .sample(materialSrg.samplers[kMaterialTextureMetallicRoughness], in.texcoord);
    const float roughness = clamp(mix(fragmentUniforms->material.x,
                                      sampledMetallicRoughness.g,
                                      sampledMetallicRoughness.a),
                                  0.08,
                                  1.0);
    const float metallic = clamp(mix(fragmentUniforms->material.y,
                                     sampledMetallicRoughness.b,
                                     sampledMetallicRoughness.a),
                                 0.0,
                                 1.0);
    const float exposure = max(fragmentUniforms->material.z, 0.001);

    const float nDotL = saturate(dot(normal, lightDir));
    const float nDotH = saturate(dot(normal, halfVec));
    const float vDotH = saturate(dot(viewDir, halfVec));

    const float3 sampledBaseColor = materialSrg.textures[kMaterialTextureBaseColor]
                                        .sample(materialSrg.samplers[kMaterialTextureBaseColor], in.texcoord)
                                        .rgb;
    const float3 baseColor = max(in.albedo * sampledBaseColor * fragmentUniforms->debugTint.rgb, float3(0.0));
    const float3 emissive = materialSrg.textures[kMaterialTextureEmissive]
                                .sample(materialSrg.samplers[kMaterialTextureEmissive], in.texcoord)
                                .rgb;
    const float occlusion = saturate(materialSrg.textures[kMaterialTextureOcclusion]
                                         .sample(materialSrg.samplers[kMaterialTextureOcclusion], in.texcoord)
                                         .r);
    const float3 f0 = mix(float3(0.04), baseColor, metallic);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    const float specPower = mix(96.0, 8.0, roughness);
    const float3 specular = fresnel * pow(nDotH, specPower) * nDotL;
    const float3 diffuse = baseColor * (1.0 - metallic) * nDotL;
    const float3 ambient = fragmentUniforms->ambientColor.xyz * baseColor * occlusion;

    const float3 hdr = (ambient + diffuse + specular + emissive) * exposure;
    return float4(ACESFilm(hdr), 1.0);
}

//Terrain materials (TerrainMaterials): two texture arrays with one layer per material and their parameters.
//The arrays and the block are bound once per frame by TerrainMaterials::Bind.

//materialAlbedo: RGB = albedo, A = height; materialDetail: RG = normal XY (OpenGL convention), B = roughness, A = ambient occlusion
uniform sampler2DArray materialAlbedo;
uniform sampler2DArray materialDetail;

//MAX_MATERIALS = TerrainMaterials::maxMaterials; the layout mirrors TerrainMaterials::Block
#define MAX_MATERIALS 16
layout(std140, binding = 0) uniform TerrainMaterialBlock {
    vec4 materialParams[MAX_MATERIALS];     //x = 1 / tileSize, y = material that replaces this one on steep slopes
    int materialCount;
    int previewMaterial;                    //material drawn everywhere without biomes
    float textureScale;                     //multiplies the tile size of every material
    float normalStrength;                   //0 = flat, 1 = normal maps as authored
    float cliffSlopeStart, cliffSlopeEnd;   //radians: materials give way to their cliff materials between these slopes
    float heightInfluence;                  //height-based blending: score = weight + heightInfluence * texture height
    float blendDepth;                       //materials within it of the best score are mixed; 0 = plain crossfade
    float triplanarStart, triplanarEnd;     //radians: the side projections fade in between these slopes
    float specularStrength;                 //0 = no highlights
    //Anti-tiling, distances in world units before modelScale:
    float farUvScale;                       //second, coarser texture scale (multiplies the first), mixed in with distance
    float farUvMix;                         //its share beyond farUvEnd; 0 = off
    float farUvStart, farUvEnd;
    float averageStart, averageEnd;         //beyond them every material fades into its average color (no shimmer, no repeats)
    float macroStrength;                    //brightness and hue variation by low-frequency noise; 0 = off
    float macroSize;                        //its wavelength
};

//One material sampled at a point of the terrain
struct MaterialSample {
    vec3 albedo;
    float height;
    vec3 normal;        //world space
    float roughness;
    float occlusion;
};

//Where a point is sampled from, shared by all materials of a pixel (see surfaceSampling)
struct SurfaceSampling {
    vec3 position;      //world units before modelScale, so the texture stays on the terrain when the model is scaled
    vec3 dPdx, dPdy;    //screen derivatives of position, taken in uniform control flow
    vec3 normal;        //unit geometric normal, world space
    vec3 projections;   //weights of the projections along X, Y (from above) and Z, summing to 1
    float farBlend;     //share of the coarse texture scale (farUvScale)
    float averageBlend; //share of the average colors
};

//Weights of the projections below SKIP_PROJECTION are dropped
const float SKIP_PROJECTION = 0.01;

//Call in uniform control flow (derivatives). Flat terrain uses the projection from above only; on slopes between
//triplanarStart and triplanarEnd the side projections fade in, so steep rock is not stretched
//@param viewDistance - distance from the camera in world units before modelScale
SurfaceSampling surfaceSampling(vec3 terrainPos, vec3 surfaceNormal, float viewDistance)
{
    SurfaceSampling s;
    s.position = terrainPos;
    s.dPdx = dFdx(terrainPos);
    s.dPdy = dFdy(terrainPos);
    s.normal = surfaceNormal;
    s.farBlend = farUvMix * smoothstep(farUvStart, max(farUvEnd, farUvStart + 1.0e-3), viewDistance);
    s.averageBlend = smoothstep(averageStart, max(averageEnd, averageStart + 1.0e-3), viewDistance);

    float slope = acos(clamp(surfaceNormal.y, -1.0, 1.0));
    float side = triplanarEnd > triplanarStart ? smoothstep(triplanarStart, triplanarEnd, slope) : (slope >= triplanarStart ? 1.0 : 0.0);
    vec3 triplanar = pow(abs(surfaceNormal), vec3(4.0));
    triplanar /= triplanar.x + triplanar.y + triplanar.z;
    vec3 w = mix(vec3(0.0, 1.0, 0.0), triplanar, side);
    w *= vec3(greaterThanEqual(w, vec3(SKIP_PROJECTION)));
    s.projections = w / (w.x + w.y + w.z);
    return s;
}

//Normal map texel as a tangent space normal, XY scaled by normalStrength
vec3 tangentNormal(vec4 detail)
{
    vec2 xy = detail.rg * 2.0 - 1.0;
    return vec3(xy * normalStrength, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

//Samples a material by up to three planar projections (triplanar). Each projection's normal is combined with the surface
//normal by the whiteout blend, with U and V along the two world axes of its plane:
//X: uv = (z, y), Y: uv = (x, z), Z: uv = (x, y). Mirrored textures on the negative sides do not matter for natural materials,
//and the normals stay right: each tangent axis is the world axis its texture coordinate follows
//@param uvScale - multiplies the texture scale of the material (anti-tiling)
MaterialSample sampleProjected(int material, SurfaceSampling p, float uvScale)
{
    float scale = materialParams[material].x / textureScale * uvScale;
    vec3 n = p.normal;
    vec4 albedo = vec4(0.0), detail = vec4(0.0);
    vec3 normal = vec3(0.0);
    if (p.projections.x > 0.0) {
        vec3 uvw = vec3(p.position.zy * scale, material);
        vec2 gx = p.dPdx.zy * scale, gy = p.dPdy.zy * scale;
        vec4 d = textureGrad(materialDetail, uvw, gx, gy);
        vec3 t = tangentNormal(d);
        albedo += p.projections.x * textureGrad(materialAlbedo, uvw, gx, gy);
        detail += p.projections.x * d;
        normal += p.projections.x * vec3(t.xy + n.zy, abs(t.z) * n.x).zyx;
    }
    if (p.projections.y > 0.0) {
        vec3 uvw = vec3(p.position.xz * scale, material);
        vec2 gx = p.dPdx.xz * scale, gy = p.dPdy.xz * scale;
        vec4 d = textureGrad(materialDetail, uvw, gx, gy);
        vec3 t = tangentNormal(d);
        albedo += p.projections.y * textureGrad(materialAlbedo, uvw, gx, gy);
        detail += p.projections.y * d;
        normal += p.projections.y * vec3(t.xy + n.xz, abs(t.z) * n.y).xzy;
    }
    if (p.projections.z > 0.0) {
        vec3 uvw = vec3(p.position.xy * scale, material);
        vec2 gx = p.dPdx.xy * scale, gy = p.dPdy.xy * scale;
        vec4 d = textureGrad(materialDetail, uvw, gx, gy);
        vec3 t = tangentNormal(d);
        albedo += p.projections.z * textureGrad(materialAlbedo, uvw, gx, gy);
        detail += p.projections.z * d;
        normal += p.projections.z * vec3(t.xy + n.xy, abs(t.z) * n.z);
    }

    MaterialSample s;
    s.albedo = albedo.rgb;
    s.height = albedo.a;
    s.normal = normalize(normal);
    s.roughness = detail.b;
    s.occlusion = detail.a;
    return s;
}

MaterialSample mixSamples(MaterialSample a, MaterialSample b, float t)
{
    MaterialSample s;
    s.albedo = mix(a.albedo, b.albedo, t);
    s.height = mix(a.height, b.height, t);
    s.normal = normalize(mix(a.normal, b.normal, t));
    s.roughness = mix(a.roughness, b.roughness, t);
    s.occlusion = mix(a.occlusion, b.occlusion, t);
    return s;
}

//Samples a material with anti-tiling:
//- with distance a second, coarser texture scale is mixed in (farBlend), so the repeats of the first one stop lining up
//- far away the material fades into its average color (the last mip level) and flat normal, which costs one texel
//  and removes both shimmer and visible repeats; beyond averageEnd only that texel is read
MaterialSample sampleMaterial(int material, SurfaceSampling p)
{
    MaterialSample s;
    if (p.averageBlend < 1.0) {
        s = sampleProjected(material, p, 1.0);
        if (p.farBlend > 0.0) {
            s = mixSamples(s, sampleProjected(material, p, farUvScale), p.farBlend);
        }
    }
    if (p.averageBlend > 0.0) {
        //A LOD past the last level is clamped to it: one texel with the average of the whole texture
        vec4 average = textureLod(materialAlbedo, vec3(0.5, 0.5, material), 32.0);
        vec4 averageDetail = textureLod(materialDetail, vec3(0.5, 0.5, material), 32.0);
        MaterialSample a;
        a.albedo = average.rgb;
        a.height = average.a;
        a.normal = p.normal;
        a.roughness = averageDetail.b;
        a.occlusion = averageDetail.a;
        s = p.averageBlend < 1.0 ? mixSamples(s, a, p.averageBlend) : a;
    }
    return s;
}

//Value noise in [-1, 1] for the macro variation (cheap hash, no tables)
float materialHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float materialValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = materialHash(i), b = materialHash(i + vec2(1.0, 0.0));
    float c = materialHash(i + vec2(0.0, 1.0)), d = materialHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

float macroNoise(vec2 p)
{
    return 0.57 * materialValueNoise(p) + 0.29 * materialValueNoise(p * 2.03 + vec2(17.1, 5.3)) + 0.14 * materialValueNoise(p * 4.01 - vec2(31.7, 11.9));
}

//Multiplier of the albedo: brightness and a slight warm/cool shift, varying over macroSize, so large areas of one
//material are not uniform
vec3 macroVariation(vec3 terrainPos)
{
    if (macroStrength <= 0.0) {
        return vec3(1.0);
    }
    vec2 q = terrainPos.xz / macroSize;
    float brightness = macroNoise(q);
    float hue = macroNoise(q * 0.7 + vec2(53.1, -27.4));
    return vec3(1.0) + macroStrength * (vec3(brightness) + hue * vec3(0.3, 0.0, -0.3));
}

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
};

//Weights of the projections below SKIP_PROJECTION are dropped
const float SKIP_PROJECTION = 0.01;

//Call in uniform control flow (derivatives). Flat terrain uses the projection from above only; on slopes between
//triplanarStart and triplanarEnd the side projections fade in, so steep rock is not stretched
SurfaceSampling surfaceSampling(vec3 terrainPos, vec3 surfaceNormal)
{
    SurfaceSampling s;
    s.position = terrainPos;
    s.dPdx = dFdx(terrainPos);
    s.dPdy = dFdy(terrainPos);
    s.normal = surfaceNormal;

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
MaterialSample sampleMaterial(int material, SurfaceSampling p)
{
    float scale = materialParams[material].x / textureScale;
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

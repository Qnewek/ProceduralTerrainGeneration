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
};

//One material sampled at a point of the terrain
struct MaterialSample {
    vec3 albedo;
    float height;
    vec3 normal;        //world space
    float roughness;
    float occlusion;
};

//Samples a material projected from above on the XZ plane.
//@param terrainPos - position in world units before modelScale, so the texture stays on the terrain when the model is scaled
//@param surfaceNormal - unit geometric normal in world space
MaterialSample sampleMaterial(int material, vec3 terrainPos, vec3 surfaceNormal)
{
    //U along +X, V along -Z: seen from above, U to the right and V up, so the tangent frame is right-handed
    vec2 uv = vec2(terrainPos.x, -terrainPos.z) * materialParams[material].x / textureScale;
    vec4 albedo = texture(materialAlbedo, vec3(uv, material));
    vec4 detail = texture(materialDetail, vec3(uv, material));

    MaterialSample s;
    s.albedo = albedo.rgb;
    s.height = albedo.a;
    s.roughness = detail.b;
    s.occlusion = detail.a;

    //Tangent frame of the planar projection on a sloped surface: T follows +X, B follows -Z, both in the surface plane
    vec3 n = surfaceNormal;
    vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
    vec3 b = cross(n, t);
    vec2 xy = detail.rg * 2.0 - 1.0;
    float z = sqrt(max(1.0 - dot(xy, xy), 0.0));
    vec3 tangentNormal = normalize(vec3(xy * normalStrength, z));
    s.normal = normalize(t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z);
    return s;
}

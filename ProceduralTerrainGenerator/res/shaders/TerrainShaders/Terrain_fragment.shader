#version 450 core

in vec3 FragPos;
in vec3 aColor;
in vec3 aNormal;
in float Height;
in vec3 TerrainPos;
in vec2 MapCoord;

out vec4 FragColor;

//Camera, sun and fog (the 2D map has no fog)
#include "../common/frame.glsl"
//Isolines: darken a band of bandWidth every step units of height (bandWidth = 0 disables them)
uniform float step;
uniform float bandWidth;
uniform mat4 model;         //model[0][0] = modelScale (as in the TES)
uniform int displayMode;    //values of utilities::heightMapMode; the colors of the other modes come from the TES
//Share of every material (GpuHeightGenerator::GenerateBiomes): material m in layer m / 4, channel m % 4.
//Without it (no biomes) the TEXTURED mode draws previewMaterial
uniform sampler2DArray splatMap;
uniform bool useSplat;
//World units around the camera with blades of grass (GrassLayer), 0 = none: the ground under them gets less light
uniform float grassRange;
uniform float grassShade;   //strength of that for the density and height of the blades, 0–1
//Normal per pixel from the surface map (the TES one is interpolated between vertices, which blurs distant, sparsely tessellated terrain)
uniform sampler2D surfaceMap;
//Biome colors per pixel too (mode 3)
uniform sampler2D biomeMap;
//Land cover classes (mode 7, Imported map): the class code of every texel and the color of every code (TerrainView::SetLandCover)
uniform usampler2D landCoverMap;
uniform bool useLandCover;
uniform vec3 landCoverColors[256];
uniform mat3 normalMatrix;
uniform float heightScale;

#include "../common/sky.glsl"
#include "../common/fog.glsl"
#include "../common/surface.glsl"
#include "../common/materials.glsl"
#include "../common/shadow.glsl"
#include "../common/light.glsl"

const int MODE_BIOMES = 3;
const int MODE_TEXTURED = 6;
const int MODE_LAND_COVER = 7;

//The color of the class of the texel under the pixel (no interpolation between classes); grey without land cover
vec3 landCoverColor()
{
    if (!useLandCover) {
        return vec3(0.5);
    }
    ivec2 size = textureSize(landCoverMap, 0);
    ivec2 texel = clamp(ivec2(MapCoord * vec2(size)), ivec2(0), size - 1);
    return landCoverColors[texelFetch(landCoverMap, texel, 0).r & 255u];
}
//Materials with a smaller share are not sampled
const float MIN_MATERIAL_WEIGHT = 0.01;
//Most materials sampled per pixel
const int SAMPLED_MATERIALS = 4;

//Surface of the TEXTURED mode:
//1. weights of the materials from the splat map (biomes), or previewMaterial alone
//2. cliffs: on slopes between cliffSlopeStart and cliffSlopeEnd each material hands its weight over to its cliff material
//3. the SAMPLED_MATERIALS strongest materials, mixed by height-based blending (score = weight + heightInfluence * texture height,
//   only materials within blendDepth of the best score show), or by their weights if blendDepth is 0
void texturedSurface(SurfaceSampling p, out vec3 albedo, out vec3 normal, out float occlusion, out float roughness)
{
    vec3 surfaceNormal = p.normal;
    float w[MAX_MATERIALS];
    float total = 0.0;
    for (int m = 0; m < MAX_MATERIALS; m++) {
        w[m] = 0.0;
    }
    if (useSplat) {
        for (int l = 0; l < MAX_MATERIALS / 4; l++) {
            vec4 shares = texture(splatMap, vec3(MapCoord, l));
            for (int c = 0; c < 4; c++) {
                int m = l * 4 + c;
                if (m < materialCount && shares[c] >= MIN_MATERIAL_WEIGHT) {
                    w[m] = shares[c];
                    total += shares[c];
                }
            }
        }
    }
    if (total <= 0.0) {
        w[clamp(previewMaterial, 0, materialCount - 1)] = 1.0;
    }

    //Moved from a copy, so weight handed to a cliff material is not handed on again
    float slope = acos(clamp(surfaceNormal.y, -1.0, 1.0));
    //Not step(): the isoline uniform "step" hides the built-in function in this shader
    float cliff = cliffSlopeEnd > cliffSlopeStart ? smoothstep(cliffSlopeStart, cliffSlopeEnd, slope) : (slope >= cliffSlopeStart ? 1.0 : 0.0);
    if (cliff > 0.0) {
        float before[MAX_MATERIALS] = w;
        for (int m = 0; m < materialCount; m++) {
            int target = int(materialParams[m].y);
            if (before[m] > 0.0 && target != m) {
                float moved = before[m] * cliff;
                w[m] -= moved;
                w[target] += moved;
            }
        }
    }

    //The strongest materials, renormalized
    int ids[SAMPLED_MATERIALS];
    float weights[SAMPLED_MATERIALS];
    float chosenTotal = 0.0;
    for (int i = 0; i < SAMPLED_MATERIALS; i++) {
        ids[i] = 0;
        weights[i] = 0.0;
        int best = -1;
        for (int m = 0; m < materialCount; m++) {
            if (w[m] >= MIN_MATERIAL_WEIGHT && (best < 0 || w[m] > w[best])) {
                best = m;
            }
        }
        if (best < 0) {
            break;
        }
        ids[i] = best;
        weights[i] = w[best];
        chosenTotal += w[best];
        w[best] = 0.0;
    }

    MaterialSample samples[SAMPLED_MATERIALS];
    float scores[SAMPLED_MATERIALS];
    float bestScore = -1.0e30;
    for (int i = 0; i < SAMPLED_MATERIALS; i++) {
        if (weights[i] <= 0.0) {
            continue;
        }
        weights[i] /= chosenTotal;
        samples[i] = sampleMaterial(ids[i], p);
        scores[i] = weights[i] + heightInfluence * samples[i].height;
        bestScore = max(bestScore, scores[i]);
    }

    albedo = vec3(0.0);
    normal = vec3(0.0);
    occlusion = 0.0;
    roughness = 0.0;
    float sum = 0.0;
    for (int i = 0; i < SAMPLED_MATERIALS; i++) {
        if (weights[i] <= 0.0) {
            continue;
        }
        float b = blendDepth > 0.0 ? max(scores[i] - (bestScore - blendDepth), 0.0) : weights[i];
        albedo += b * samples[i].albedo;
        normal += b * samples[i].normal;
        occlusion += b * samples[i].occlusion;
        roughness += b * samples[i].roughness;
        sum += b;
    }
    //sum > 0: the best material has b = blendDepth (or its weight)
    albedo = clamp(albedo / sum * macroVariation(p.position), 0.0, 1.0);
    normal = normalize(normal);
    occlusion /= sum;
    roughness /= sum;
}

//Share of the ground under blades of grass: material weights from the splat map times their grass, with the weight that steep
//slopes hand over to cliff materials (as texturedSurface does) taking the grass of the cliff material. 0 without biomes
//(GrassLayer grows blades only from the splat map)
float grassCover(vec3 surfaceNormal)
{
    if (!useSplat) {
        return 0.0;
    }
    float slope = acos(clamp(surfaceNormal.y, -1.0, 1.0));
    float cliff = cliffSlopeEnd > cliffSlopeStart ? smoothstep(cliffSlopeStart, cliffSlopeEnd, slope) : (slope >= cliffSlopeStart ? 1.0 : 0.0);
    float cover = 0.0;
    float total = 0.0;
    for (int l = 0; l < MAX_MATERIALS / 4; l++) {
        vec4 shares = texture(splatMap, vec3(MapCoord, l));
        for (int c = 0; c < 4; c++) {
            int m = l * 4 + c;
            if (m < materialCount && shares[c] >= MIN_MATERIAL_WEIGHT) {
                int target = int(materialParams[m].y);
                float grass = target != m ? mix(materialParams[m].z, materialParams[target].z, cliff) : materialParams[m].z;
                cover += shares[c] * grass;
                total += shares[c];
            }
        }
    }
    return total > 0.0 ? clamp(cover / total, 0.0, 1.0) : 0.0;
}

//Blinn-Phong highlight of the sun, sharper and stronger on smooth surfaces (wet sand, polished rock, snow crust);
//most terrain materials are rough, so it stays a faint sheen there
float sunSpecular(vec3 n, vec3 toLight, vec3 toViewer, float roughness)
{
    float smoothness = 1.0 - roughness;
    float shininess = exp2(1.0 + 10.0 * smoothness);
    vec3 h = normalize(toLight + toViewer);
    return specularStrength * smoothness * pow(max(dot(n, h), 0.0), shininess);
}

void main()
{
    //Shadow map: only the depth counts
    if (depthPass) {
        return;
    }
    vec3 baseColor = displayMode == MODE_BIOMES ? texture(biomeMap, MapCoord).rgb : displayMode == MODE_LAND_COVER ? landCoverColor() : aColor;
    vec3 norm = normalize(normalMatrix * surfaceNormal(texture(surfaceMap, MapCoord), heightScale));
    vec3 geometryNormal = norm;
    float occlusion = 1.0;
    float roughness = 1.0;
    bool textured = displayMode == MODE_TEXTURED;
    //Ground under blades of grass, in the modes that show the surface (not in the analysis ones); fades out where the blades
    //get shorter (grass_compute.shader), by the same horizontal distance
    float grass = 0.0;
    if (grassRange > 0.0 && (textured || displayMode == MODE_BIOMES)) {
        float flatDistance = length((viewPos - FragPos).xz) / model[0][0];
        float fade = 1.0 - smoothstep(0.8 * grassRange, grassRange, flatDistance);
        grass = fade > 0.0 ? grassCover(norm) * fade * grassShade : 0.0;
    }
    if (textured) {
        //displayMode is uniform, so the derivatives inside surfaceSampling are taken in uniform control flow
        float viewDistance = length(viewPos - FragPos) / model[0][0];
        texturedSurface(surfaceSampling(TerrainPos, norm, viewDistance), baseColor, norm, occlusion, roughness);
    }
    else {
        float modHeight = mod(Height, step);
        if (modHeight < bandWidth && Height >= step) {
            baseColor *= 0.5;
        }
    }

    vec3 result = baseColor;
    if (lightOn) {
        float diff = max(dot(norm, light.direction), 0.0);
        //The colors set in the UI change with the sun height, like the sky does
        vec3 ambient = light.ambient * ambientLightTint();
        vec3 sunlight = light.diffuse * directLightTint() * sunShadow(FragPos, geometryNormal, 1);
        //The ground between blades of grass: a low sun crosses more blades before it reaches it, the sky is partly hidden
        float sunUp = clamp(light.direction.y, 0.0, 1.0);
        sunlight *= 1.0 - grass * mix(0.85, 0.45, sunUp);
        ambient *= 1.0 - grass * 0.4;
        result = (ambient * occlusion + sunlight * diff) * baseColor;
        if (textured && diff > 0.0) {
            result += sunlight * diff * sunSpecular(norm, light.direction, normalize(viewPos - FragPos), roughness);
        }
    }

    result = applyFog(result, FragPos);
    FragColor = vec4(result, 1.0);
}

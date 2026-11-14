#version 450 core

in vec3 FragPos;
in vec3 aColor;
in vec3 aNormal;
in float Height;
in vec3 TerrainPos;
in vec2 MapCoord;

out vec4 FragColor;

//The sun: a directional light, the same everywhere
struct Light {
    vec3 direction;     //unit vector towards the sun
    vec3 ambient;
    vec3 diffuse;
};

uniform bool lightOn;
uniform Light light;
//Isolines: darken a band of bandWidth every step units of height (bandWidth = 0 disables them)
uniform float step;
uniform float bandWidth;
//Fog: the terrain fades into the sky color between fogStart and fogDistance from the camera
uniform bool fogOn;
uniform float fogStart;
uniform float fogDistance;
uniform vec3 viewPos;
uniform mat4 model;         //model[0][0] = modelScale (as in the TES)
uniform int displayMode;    //values of utilities::heightMapMode; the colors of the other modes come from the TES
//Share of every material (GpuHeightGenerator::GenerateBiomes): material m in layer m / 4, channel m % 4.
//Without it (no biomes) the TEXTURED mode draws previewMaterial
uniform sampler2DArray splatMap;
uniform bool useSplat;
//Normal per pixel from the surface map (the TES one is interpolated between vertices, which blurs distant, sparsely tessellated terrain)
uniform sampler2D surfaceMap;
//Biome colors per pixel too (mode 3)
uniform sampler2D biomeMap;
uniform mat3 normalMatrix;
uniform float heightScale;

#include "../common/sky.glsl"
#include "../common/surface.glsl"
#include "../common/materials.glsl"

const int MODE_BIOMES = 3;
const int MODE_TEXTURED = 6;
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
    vec3 baseColor = displayMode == MODE_BIOMES ? texture(biomeMap, MapCoord).rgb : aColor;
    vec3 norm = normalize(normalMatrix * surfaceNormal(texture(surfaceMap, MapCoord), heightScale));
    float occlusion = 1.0;
    float roughness = 1.0;
    bool textured = displayMode == MODE_TEXTURED;
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
        vec3 ambient = light.ambient * ambientTint(light.direction);
        vec3 sunlight = light.diffuse * sunlightTint(light.direction);
        result = (ambient * occlusion + sunlight * diff) * baseColor;
        if (textured && diff > 0.0) {
            result += sunlight * diff * sunSpecular(norm, light.direction, normalize(viewPos - FragPos), roughness);
        }
    }

    if (fogOn) {
        vec3 toFragment = FragPos - viewPos;
        float fog = smoothstep(fogStart, fogDistance, length(toFragment));
        result = mix(result, fogColor(toFragment, light.direction), fog);
    }
    FragColor = vec4(result, 1.0);
}

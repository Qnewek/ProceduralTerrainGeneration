#version 450 core

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;

layout(location = 0) out vec4 FragColor;
//Only while baking impostors (VegetationModels::BakeImpostors): normal in model space as RGB = n * 0.5 + 0.5, A = 1
layout(location = 1) out vec4 NormalOut;

#include "../common/frame.glsl"

//Maps of the material (VegetationModels): RGB = albedo, A = alpha; RG = normal XY (OpenGL convention), B = roughness, A = ambient occlusion
uniform sampler2D albedoMap;
uniform sampler2D detailMap;
uniform bool alphaTest;
//Leaves and needles: light passing through from behind and softer shading
uniform bool foliage;
uniform float normalStrength;
uniform float specularStrength;
//Baking impostors: unlit albedo with alpha 1 to attachment 0 and the normal to attachment 1, the model drawn with yaw 0 at the origin
uniform bool bake;

#include "../common/sky.glsl"
#include "../common/shadow.glsl"

//Alpha of distant mip levels is averaged down, so thin leaves would vanish; raising it with the mip level keeps their coverage
const float ALPHA_MIP_BOOST = 0.25;
//Share of the sunlight that foliage lets through to its back side
const float TRANSLUCENCY = 0.35;

//Tangent frame from screen-space derivatives (no tangents in the mesh)
mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv)
{
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(t, t), dot(b, b)));
    return mat3(t * invmax, b * invmax, n);
}

void main()
{
    vec4 albedo = texture(albedoMap, UV);
    //Shadow map: only the alpha test counts (no detail map, no tangent frame)
    if (depthPass) {
        if (alphaTest && albedo.a * (1.0 + max(textureQueryLod(albedoMap, UV).x, 0.0) * ALPHA_MIP_BOOST) < 0.5) {
            discard;
        }
        return;
    }
    vec4 detail = texture(detailMap, UV);
    //Derivatives before any discard
    vec3 n = normalize(Normal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    mat3 tbn = cotangentFrame(n, FragPos, UV);
    if (alphaTest) {
        float lod = textureQueryLod(albedoMap, UV).x;
        if (albedo.a * (1.0 + max(lod, 0.0) * ALPHA_MIP_BOOST) < 0.5) {
            discard;
        }
    }
    //glTF texture coordinates grow down the image, the frame's second axis follows them; green of an OpenGL normal map points up the image
    vec3 tangentNormal = vec3((detail.xy * 2.0 - 1.0) * vec2(1.0, -1.0) * normalStrength, 0.0);
    tangentNormal.z = sqrt(max(1.0 - dot(tangentNormal.xy, tangentNormal.xy), 0.0));
    vec3 norm = normalize(tbn * tangentNormal);
    float occlusion = detail.a;
    float roughness = detail.b;
    if (bake) {
        //Plain albedo: the impostor darkens only its ambient light by an average occlusion, as the mesh does per pixel
        FragColor = vec4(albedo.rgb, 1.0);
        NormalOut = vec4(norm * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 result = albedo.rgb;
    if (lightOn) {
        float facing = dot(norm, light.direction);
        float diff = max(facing, 0.0);
        if (foliage) {
            diff += max(-facing, 0.0) * TRANSLUCENCY;
        }
        vec3 ambient = light.ambient * ambientTint(light.direction);
        vec3 sunlight = light.diffuse * sunlightTint(light.direction) * sunShadow(FragPos, n, 0);
        result = (ambient * occlusion + sunlight * diff) * albedo.rgb;
        if (facing > 0.0) {
            float smoothness = 1.0 - roughness;
            float shininess = exp2(1.0 + 10.0 * smoothness);
            vec3 h = normalize(light.direction + normalize(viewPos - FragPos));
            result += sunlight * facing * specularStrength * smoothness * pow(max(dot(norm, h), 0.0), shininess);
        }
    }
    if (fogOn) {
        vec3 toFragment = FragPos - viewPos;
        float fog = smoothstep(fogStart, fogDistance, length(toFragment));
        result = mix(result, fogColor(toFragment, light.direction), fog);
    }
    FragColor = vec4(result, 1.0);
}

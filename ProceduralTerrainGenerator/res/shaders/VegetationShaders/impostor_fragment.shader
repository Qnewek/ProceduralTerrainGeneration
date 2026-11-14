#version 450 core

in vec3 FragPos;
in vec2 FrameUV;
flat in ivec2 frame0;
flat in vec2 frameBlend;
flat in vec2 turnCosSin;

out vec4 FragColor;

struct Light {
    vec3 direction;     //unit vector towards the sun
    vec3 ambient;
    vec3 diffuse;
};
uniform bool lightOn;
uniform Light light;
uniform bool fogOn;
uniform float fogStart;
uniform float fogDistance;
uniform vec3 viewPos;

//Atlas of the model, one layer per variant (VegetationModels::BakeImpostors), both premultiplied by the coverage in A:
//albedo RGB, and the normal in model space as RGB = n * 0.5 + 0.5
uniform sampler2DArray impostorAlbedo;
uniform sampler2DArray impostorNormal;
uniform float layer;
uniform int frames;

#include "../common/sky.glsl"

//As in vegetation_fragment.shader: alpha of distant mip levels raised, light let through to the back of the leaves
const float ALPHA_MIP_BOOST = 0.25;
const float TRANSLUCENCY = 0.35;

void main()
{
    vec4 albedo = vec4(0.0);
    vec4 normal = vec4(0.0);
    float lod = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 offset = ivec2(k & 1, k >> 1);
        vec2 w2 = mix(1.0 - frameBlend, frameBlend, vec2(offset));
        float weight = w2.x * w2.y;
        vec3 uv = vec3((vec2(frame0 + offset) + FrameUV) / float(frames), layer);
        albedo += weight * texture(impostorAlbedo, uv);
        normal += weight * texture(impostorNormal, uv);
        if (k == 0) {
            lod = textureQueryLod(impostorAlbedo, uv.xy).x;
        }
    }
    if (albedo.a * (1.0 + max(lod, 0.0) * ALPHA_MIP_BOOST) < 0.5) {
        discard;
    }
    vec3 color = albedo.rgb / max(albedo.a, 1e-3);
    vec3 n = normal.rgb / max(normal.a, 1e-3) * 2.0 - 1.0;
    float c = turnCosSin.x, s = turnCosSin.y;
    vec3 norm = normalize(mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c) * n);

    vec3 result = color;
    if (lightOn) {
        float facing = dot(norm, light.direction);
        float diff = max(facing, 0.0) + max(-facing, 0.0) * TRANSLUCENCY;
        vec3 ambient = light.ambient * ambientTint(light.direction);
        vec3 sunlight = light.diffuse * sunlightTint(light.direction);
        result = (ambient + sunlight * diff) * color;
    }
    if (fogOn) {
        vec3 toFragment = FragPos - viewPos;
        float fog = smoothstep(fogStart, fogDistance, length(toFragment));
        result = mix(result, fogColor(toFragment, light.direction), fog);
    }
    FragColor = vec4(result, 1.0);
}

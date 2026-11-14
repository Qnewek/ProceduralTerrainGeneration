#version 450 core

in vec3 FragPos;
in vec3 Normal;
in vec3 Color;
in float Along;

out vec4 FragColor;

#include "../common/frame.glsl"
#include "../common/sky.glsl"
#include "../common/fog.glsl"
#include "../common/shadow.glsl"

//Share of the sunlight that passes through a blade lit from behind
const float TRANSLUCENCY = 0.35;

void main()
{
    vec3 n = normalize(Normal);
    //Darker at the base, where the blades shade each other
    vec3 albedo = Color * mix(0.6, 1.1, Along);
    vec3 result = albedo;
    if (lightOn) {
        float facing = dot(n, light.direction);
        float diff = max(facing, 0.0) + max(-facing, 0.0) * TRANSLUCENCY;
        vec3 ambient = light.ambient * ambientTint(light.direction);
        vec3 sunlight = light.diffuse * sunlightTint(light.direction) * sunShadow(FragPos, vec3(0.0, 1.0, 0.0), 0);
        result = (ambient + sunlight * diff) * albedo;
    }
    result = applyFog(result, FragPos);
    FragColor = vec4(result, 1.0);
}

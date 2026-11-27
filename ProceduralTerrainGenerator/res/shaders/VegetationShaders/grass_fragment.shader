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
#include "../common/light.glsl"

//Share of the sunlight that passes through a blade lit from behind
const float TRANSLUCENCY = 0.35;

void main()
{
    vec3 n = normalize(Normal);
    vec3 albedo = Color * mix(0.75, 1.05, Along);
    vec3 result = albedo;
    if (lightOn) {
        float facing = dot(n, light.direction);
        float diff = max(facing, 0.0) + max(-facing, 0.0) * TRANSLUCENCY;
        //Lower down the neighbouring blades hide the light: the sun more when it is low (its path through the grass is longer),
        //the sky a little
        float sunUp = clamp(light.direction.y, 0.0, 1.0);
        float directKeep = mix(mix(0.2, 0.65, sunUp), 1.0, Along);
        vec3 ambient = light.ambient * ambientLightTint() * mix(0.8, 1.0, Along);
        vec3 sunlight = light.diffuse * directLightTint() * sunShadow(FragPos, vec3(0.0, 1.0, 0.0), 0) * directKeep;
        result = (ambient + sunlight * diff) * albedo;
    }
    result = applyFog(result, FragPos);
    FragColor = vec4(result, 1.0);
}

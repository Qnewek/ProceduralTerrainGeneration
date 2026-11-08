#version 450 core

in vec3 FragPos;
in vec3 aColor;
in vec3 aNormal;
in float Height;

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

#include "../common/sky.glsl"

void main()
{
    vec3 baseColor = aColor;
    float modHeight = mod(Height, step);
    if (modHeight < bandWidth && Height >= step) {
        baseColor *= 0.5;
    }

    vec3 result = baseColor;
    if (lightOn) {
        vec3 norm = normalize(aNormal);
        float diff = max(dot(norm, light.direction), 0.0);
        //The colors set in the UI change with the sun height, like the sky does
        vec3 ambient = light.ambient * ambientTint(light.direction);
        vec3 sunlight = light.diffuse * sunlightTint(light.direction);
        result = (ambient + sunlight * diff) * baseColor;
    }

    if (fogOn) {
        vec3 toFragment = FragPos - viewPos;
        float fog = smoothstep(fogStart, fogDistance, length(toFragment));
        result = mix(result, fogColor(toFragment, light.direction), fog);
    }
    FragColor = vec4(result, 1.0);
}

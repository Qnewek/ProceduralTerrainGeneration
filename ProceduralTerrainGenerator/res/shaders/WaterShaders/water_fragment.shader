#version 450 core
//Water: depth from the height map of the chunk (water level - ground), giving the color of the water body, how much of the ground shows
//through (blended over the terrain drawn before) and foam at the shore. The surface reflects the sky (the same colors as the background
//and the fog) and the sun, by Fresnel; waves are normals from a few moving sine waves. Output premultiplied by alpha

in vec3 FragPos;
in vec2 WaterXZ;
in vec2 MapUV;

out vec4 FragColor;

#include "../common/frame.glsl"
#include "../common/sky.glsl"
#include "../common/shadow.glsl"

uniform sampler2D heightMap;
uniform float heightScale;
uniform float waterLevel;       //world units
uniform float unitsPerMetre;
uniform float clarity;          //metres of water through which a third of the ground still shows
uniform float waveStrength;     //slope of the waves, 1 = default
uniform vec3 shallowColor;
uniform vec3 deepColor;
uniform bool flatView;          //2D map: seen from straight above (viewPos is the 3D camera, not the map view)

const int WAVES = 5;
//Direction (xy), wavelength in metres (z), slope (w)
const vec4 WAVE[WAVES] = vec4[](
    vec4(0.80, 0.60, 9.0, 0.10),
    vec4(-0.45, 0.89, 5.3, 0.08),
    vec4(0.97, -0.24, 3.1, 0.07),
    vec4(-0.70, -0.71, 1.7, 0.05),
    vec4(0.26, 0.97, 0.9, 0.04)
);

//Slope of the surface (d height / dx, d height / dz) at p (metres)
//@param footprint - metres of water covered by a pixel; a wave fades out before it gets shorter than a few pixels (no moire)
vec2 waveSlope(vec2 p, float footprint)
{
    vec2 slope = vec2(0.0);
    for (int i = 0; i < WAVES; i++) {
        float k = 6.2831853 / WAVE[i].z;
        float speed = sqrt(9.81 / k);                   //deep water waves
        //Crests bent along their length, so they are not straight parallel lines
        vec2 along = vec2(-WAVE[i].y, WAVE[i].x);
        float bend = 1.6 * sin(dot(along, p) * 0.9 / WAVE[i].z + float(i) * 1.7) + 0.8 * sin(dot(along, p) * 0.31 / WAVE[i].z + float(i));
        float phase = k * dot(WAVE[i].xy, p) - k * speed * time + bend;
        float fade = 1.0 - smoothstep(0.08, 0.3, footprint / WAVE[i].z);
        slope += WAVE[i].w * cos(phase) * WAVE[i].xy * fade;
    }
    return slope * waveStrength;
}

void main()
{
    float ground = texture(heightMap, MapUV).r * heightScale;
    float depth = (waterLevel - ground) / unitsPerMetre;
    if (depth <= 0.0) {
        discard;
    }
    vec3 toEye = viewPos - FragPos;
    float distanceToEye = length(toEye);
    vec3 v = flatView ? vec3(0.0, 1.0, 0.0) : toEye / distanceToEye;
    vec2 p = WaterXZ / unitsPerMetre;
    vec2 footprint2 = fwidth(p);
    vec2 slope = waveSlope(p, max(footprint2.x, footprint2.y));
    vec3 n = normalize(vec3(-slope.x, 1.0, -slope.y));

    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 r = reflect(-v, n);
    r.y = abs(r.y);     //waves never reflect the ground below the horizon
    vec3 reflection = skyColor(r, light.direction);
    vec3 sunlight = light.diffuse * sunlightTint(light.direction) * sunShadow(FragPos, vec3(0.0, 1.0, 0.0), 0);
    vec3 ambient = light.ambient * ambientTint(light.direction);
    float sunHeight = max(light.direction.y, 0.0);
    if (lightOn) {
        reflection += sunlight * pow(max(dot(r, light.direction), 0.0), 600.0) * 4.0 * smoothstep(-0.05, 0.1, light.direction.y);
    }

    //The water body: lit from above, darker and bluer with depth; the ground shows through by its transmittance
    vec3 body = mix(shallowColor, deepColor, 1.0 - exp(-depth / 8.0));
    body *= lightOn ? ambient + sunlight * sunHeight : vec3(1.0);
    float transmittance = exp(-depth * 1.1 / max(clarity, 0.01));
    //Seen from above clear shallow water would vanish; on a map the sea stays clearly marked
    if (flatView) {
        transmittance = min(transmittance, 0.45);
    }

    vec3 color = fresnel * reflection + (1.0 - fresnel) * (1.0 - transmittance) * body;
    float alpha = 1.0 - (1.0 - fresnel) * transmittance;

    //Foam: a line at the shore and thin bands running towards it in the last 0.3 m of depth (wide shallow flats stay clear)
    float shore = 1.0 - smoothstep(0.0, 0.3, depth);
    float bands = 0.5 + 0.5 * sin(depth * 25.0 - time * 1.6 + sin(p.x * 0.7) + sin(p.y * 0.9));
    //Bands thinner than a pixel (far away, 2D map) become their average instead of noise
    float bandFade = 1.0 - smoothstep(0.5, 1.5, fwidth(depth * 25.0));
    bands = mix(0.5, bands, bandFade);
    float foam = shore * smoothstep(0.6, 0.9, bands + shore * 0.35) * 0.85;
    vec3 foamColor = vec3(0.92) * (lightOn ? ambient + sunlight * sunHeight : vec3(1.0));
    color = mix(color, foamColor, foam);
    alpha = mix(alpha, 1.0, foam);

    if (fogOn) {
        float fog = smoothstep(fogStart, fogDistance, distanceToEye);
        color = mix(color, fogColor(-toEye, light.direction), fog);
        alpha = mix(alpha, 1.0, fog);
    }
    FragColor = vec4(color, alpha);
}

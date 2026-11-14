#version 450 core
//Water: depth from the height map under it (water level - ground) gives the color of the water body and foam at the shore. The ground
//under the water is the copied scene shifted by the waves (refraction), seen through the water along the view ray. The surface reflects
//what the ray finds in the copied scene (screen-space reflections), or else the sky (the same colors as the background and the fog) and
//the sun, by Fresnel; waves are normals from a few moving sine waves. Opaque output: the scene under it is mixed in here

in vec3 FragPos;
in vec2 WaterXZ;
in vec2 MapUV;

out vec4 FragColor;

#include "../common/frame.glsl"
#include "../common/sky.glsl"
#include "../common/fog.glsl"
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
//Copies of the scene drawn before the water, over the viewport (WaterSurface::CopyScene)
uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;
uniform vec4 viewportRect;      //xy = lower-left corner in window pixels, zw = size
uniform mat4 inverseProjection;
uniform float modelScale;
uniform float refraction;       //shift of the ground under the water by the waves, 1 = default
uniform bool reflections;       //screen-space reflections; off = the sky only

//Screen-space reflections: steps along the ray, each longer by SSR_GROWTH, then a binary search of the hit
const int SSR_STEPS = 40;
const float SSR_GROWTH = 1.12;
const int SSR_REFINE = 6;
//Metres behind a surface a ray may pass and still hit it (plus 1% of the distance)
const float SSR_THICKNESS = 3.0;

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

//Point of the copied scene at screen coordinates uv, in view space (depth 1 = nothing drawn, the sky)
vec3 sceneViewPosition(vec2 uv)
{
    float d = texture(sceneDepth, uv).r;
    vec4 p = inverseProjection * vec4(vec3(uv, d) * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

vec2 screenOf(vec3 viewPosition)
{
    vec4 clip = projection * vec4(viewPosition, 1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

//Follows a ray in view space through the copied depth; true and the screen position of the first surface it passes behind.
//A ray passing far behind a surface (behind a thin tree, under the terrain seen from above) goes on instead
//@param metre - view space units per metre
bool traceReflection(vec3 origin, vec3 dir, float metre, out vec2 hitUV)
{
    float stepLength = max(0.25 * metre, -origin.z * 0.02);
    vec3 previous = origin;
    vec3 p = origin;
    for (int i = 0; i < SSR_STEPS; i++) {
        previous = p;
        p += dir * stepLength;
        if ((projection * vec4(p, 1.0)).w <= 0.0) {
            return false;
        }
        vec2 uv = screenOf(p);
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            return false;
        }
        if (texture(sceneDepth, uv).r < 1.0) {
            float behind = sceneViewPosition(uv).z - p.z;
            if (behind > 0.0) {
                vec3 a = previous, b = p;
                for (int k = 0; k < SSR_REFINE; k++) {
                    vec3 middle = 0.5 * (a + b);
                    vec2 m = screenOf(middle);
                    if (sceneViewPosition(m).z - middle.z > 0.0) {
                        b = middle;
                    }
                    else {
                        a = middle;
                    }
                }
                //A hit only where the ray meets the surface; a ray passing far behind it (over a ridge into the sky) goes on
                hitUV = screenOf(b);
                float gap = sceneViewPosition(hitUV).z - b.z;
                if (gap < SSR_THICKNESS * metre + 0.01 * length(b)) {
                    return true;
                }
            }
        }
        stepLength *= SSR_GROWTH;
    }
    return false;
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

    float metre = unitsPerMetre * modelScale;     //view space units per metre
    vec2 screenUV = (gl_FragCoord.xy - viewportRect.xy) / viewportRect.zw;
    vec3 surfaceView = (view * vec4(FragPos, 1.0)).xyz;
    float surfaceDistance = length(surfaceView);

    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 r = reflect(-v, n);
    r.y = abs(r.y);     //waves never reflect the ground below the horizon
    vec3 sky = skyColor(r, light.direction);
    if (reflections && !flatView) {
        //The sky with its clouds, where the direction of the reflection is on the screen and nothing stands in front of the sky there
        vec3 skyView = mat3(view) * r;
        if (skyView.z < -1e-4) {
            vec4 clip = projection * vec4(skyView, 0.0);
            vec2 skyUV = clip.xy / clip.w * 0.5 + 0.5;
            vec2 edge = min(skyUV, 1.0 - skyUV);
            float onScreen = smoothstep(0.0, 0.08, min(edge.x, edge.y));
            if (onScreen > 0.0 && texture(sceneDepth, skyUV).r >= 1.0) {
                sky = mix(sky, texture(sceneColor, skyUV).rgb, onScreen);
            }
        }
    }
    vec3 sunlight = light.diffuse * sunlightTint(light.direction) * sunShadow(FragPos, vec3(0.0, 1.0, 0.0), 0);
    vec3 ambient = light.ambient * ambientTint(light.direction);
    float sunHeight = max(light.direction.y, 0.0);
    if (lightOn) {
        sky += sunlight * pow(max(dot(r, light.direction), 0.0), 600.0) * 4.0 * smoothstep(-0.05, 0.1, light.direction.y);
    }
    vec3 reflection = sky;
    if (reflections) {
        //Traced for the calm surface: a clean mirror image that stays still as the waves move (a ray per wave facet flickers between
        //the far shore and the sky); the waves only ripple where the image is read
        vec3 viewRay = normalize(mat3(view) * reflect(-v, vec3(0.0, 1.0, 0.0)));
        vec2 hit;
        //Rays turning towards the camera only find what stands in front of the water (the copy holds nothing behind it)
        float away = 1.0 - smoothstep(-0.15, 0.0, viewRay.z);
        if (away > 0.0 && traceReflection(surfaceView, viewRay, metre, hit)) {
            vec2 rippled = clamp(hit + vec2(slope.x, -slope.y) * 0.08 / (1.0 + surfaceDistance / (40.0 * metre)), vec2(0.0), vec2(1.0));
            if (texture(sceneDepth, rippled).r >= 1.0) {
                rippled = hit;
            }
            //Faded out at the edges of the screen
            vec2 edge = min(rippled, 1.0 - rippled);
            float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * away;
            reflection = mix(sky, texture(sceneColor, rippled).rgb, fade);
        }
    }

    //Ground under the water: the copied scene shifted by the waves, more where there is more water to bend the view through
    vec3 seenPoint = sceneViewPosition(screenUV);
    float thickness = max(length(seenPoint) - surfaceDistance, 0.0) / metre;
    vec2 shift = slope * refraction * 0.04 * min(thickness, 4.0) / (1.0 + surfaceDistance / (20.0 * metre));
    vec2 refractedUV = clamp(screenUV + shift, vec2(0.0), vec2(1.0));
    vec3 refractedPoint = sceneViewPosition(refractedUV);
    //A shift onto something in front of the water (a tree on the shore) would show it under the water
    if (length(refractedPoint) < surfaceDistance) {
        refractedUV = screenUV;
        refractedPoint = seenPoint;
    }
    thickness = max(length(refractedPoint) - surfaceDistance, 0.0) / metre;
    vec3 under = texture(sceneColor, refractedUV).rgb;

    //The water body: lit from above, darker and bluer with depth; the ground shows through by its transmittance along the view
    vec3 body = mix(shallowColor, deepColor, 1.0 - exp(-depth / 8.0));
    body *= lightOn ? ambient + sunlight * sunHeight : vec3(1.0);
    float transmittance = exp(-thickness * 1.1 / max(clarity, 0.01));
    //Seen from above clear shallow water would vanish; on a map the sea stays clearly marked
    if (flatView) {
        transmittance = min(transmittance, 0.45);
    }

    vec3 color = fresnel * reflection + (1.0 - fresnel) * mix(body, under, transmittance);

    //Foam: a line at the shore and thin bands running towards it in the last 0.3 m of depth (wide shallow flats stay clear)
    float shore = 1.0 - smoothstep(0.0, 0.3, depth);
    float bands = 0.5 + 0.5 * sin(depth * 25.0 - time * 1.6 + sin(p.x * 0.7) + sin(p.y * 0.9));
    //Bands thinner than a pixel (far away, 2D map) become their average instead of noise
    float bandFade = 1.0 - smoothstep(0.5, 1.5, fwidth(depth * 25.0));
    bands = mix(0.5, bands, bandFade);
    float foam = shore * smoothstep(0.6, 0.9, bands + shore * 0.35) * 0.85;
    vec3 foamColor = vec3(0.92) * (lightOn ? ambient + sunlight * sunHeight : vec3(1.0));
    color = mix(color, foamColor, foam);

    color = applyFog(color, FragPos);
    FragColor = vec4(color, 1.0);
}

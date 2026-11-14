#version 450 core
//Sky-view look-up table (atmosphere_model.glsl): light scattered towards the camera along every direction, by a march through the
//atmosphere with the sunlight from the transmittance table. Every frame, in two passes:
//pass 0 - the sky (rows 0..SKY_VIEW_SIZE.y-1), the sunlight tint and the transmittance towards the sun (row SKY_COLORS_ROW);
//pass 1 - one invocation: the ambient tint = light from the whole sky on a level surface, summed from the table of pass 0

layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, binding = 0) uniform image2D skyViewLut;
layout(binding = 14) uniform sampler2D transmittanceLut;     //Sky::skyViewUnit while computing

uniform float cameraRadius;     //km from the centre of the planet
uniform vec3 sunDirection;      //unit vector towards the sun, world space
uniform int pass;

#include "../common/atmosphere_model.glsl"

const int STEPS = 32;
//Light scattered more than once, as a share of the once scattered light, spread evenly (cheap stand-in for a multiple scattering table)
const float MULTIPLE_SCATTERING = 0.15;
//Ambient light (pass 1) of a sun 45 degrees high; the ambient tint is 1 then
const vec3 AMBIENT_REFERENCE = vec3(0.0267, 0.0489, 0.0885);

//Light of the sun left at p (0 in the shadow of the planet)
vec3 sunTransmittance(vec3 p, vec3 sun)
{
    float r = length(p);
    if (raySphere(p, sun, GROUND_RADIUS) > 0.0) {
        return vec3(0.0);
    }
    return texture(transmittanceLut, transmittanceUV(r, dot(p, sun) / r)).rgb;
}

vec3 skyRadiance(vec3 origin, vec3 direction, vec3 sun)
{
    float ground = raySphere(origin, direction, GROUND_RADIUS);
    float rayLength = ground > 0.0 ? ground : max(raySphere(origin, direction, TOP_RADIUS), 0.0);
    float cosAngle = dot(direction, sun);
    float phaseR = rayleighPhase(cosAngle), phaseM = miePhase(cosAngle);
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    for (int i = 0; i < STEPS; i++) {
        //Denser near the camera, where the air is thickest
        float t0 = rayLength * pow(float(i) / float(STEPS), 2.0);
        float t1 = rayLength * pow(float(i + 1) / float(STEPS), 2.0);
        vec3 p = origin + direction * 0.5 * (t0 + t1);
        Medium m = mediumAt(max(0.0, length(p) - GROUND_RADIUS));
        vec3 sunlight = sunTransmittance(p, sun);
        vec3 scattering = (m.rayleigh * phaseR + m.mie * phaseM) * sunlight
            + (m.rayleigh + m.mie) * sunlight * (MULTIPLE_SCATTERING / (4.0 * PI));
        //Integrated over the step (energy conserving, Hillaire)
        vec3 stepTransmittance = exp(-m.extinction * (t1 - t0));
        radiance += throughput * (scattering - scattering * stepTransmittance) / max(m.extinction, vec3(1e-7));
        throughput *= stepTransmittance;
    }
    return radiance;
}

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    //The sun in a frame turned so that it lies towards +X
    vec3 sun = vec3(length(sunDirection.xz), sunDirection.y, 0.0);
    vec3 origin = vec3(0.0, cameraRadius, 0.0);

    if (pass == 1) {
        if (texel != ivec2(0)) {
            return;
        }
        //Irradiance of a level surface: sky radiance * cos(zenith angle) over the upper half of the table (both sides of the sun)
        const int AZIMUTHS = 24, ELEVATIONS = 12;
        vec3 irradiance = vec3(0.0);
        for (int j = 0; j < ELEVATIONS; j++) {
            float elevation = (float(j) + 0.5) / float(ELEVATIONS) * 0.5 * PI;
            int y = int(skyViewV(elevation) * float(SKY_VIEW_SIZE.y - 1) + 0.5);
            float weight = sin(elevation) * cos(elevation) * (0.5 * PI / float(ELEVATIONS)) * (PI / float(AZIMUTHS)) * 2.0;
            for (int i = 0; i < AZIMUTHS; i++) {
                int x = int((float(i) + 0.5) / float(AZIMUTHS) * float(SKY_VIEW_SIZE.x - 1) + 0.5);
                irradiance += imageLoad(skyViewLut, ivec2(x, y)).rgb * weight;
            }
        }
        imageStore(skyViewLut, ivec2(1, SKY_COLORS_ROW), vec4(irradiance / AMBIENT_REFERENCE, 1.0));
        return;
    }

    if (texel.x >= SKY_VIEW_SIZE.x || texel.y >= SKY_VIEW_ROWS) {
        return;
    }
    if (texel.y == SKY_COLORS_ROW) {
        vec3 toSun = sunTransmittance(origin, sun);
        if (texel.x == 0) {
            //White for a sun at the zenith
            vec3 zenith = texture(transmittanceLut, transmittanceUV(cameraRadius, 1.0)).rgb;
            imageStore(skyViewLut, texel, vec4(toSun / zenith, 1.0));
        }
        else if (texel.x == 2) {
            imageStore(skyViewLut, texel, vec4(toSun, 1.0));
        }
        return;
    }
    float azimuth = float(texel.x) / float(SKY_VIEW_SIZE.x - 1) * PI;
    float elevation = skyViewElevation(float(texel.y) / float(SKY_VIEW_SIZE.y - 1));
    vec3 direction = vec3(cos(elevation) * cos(azimuth), sin(elevation), cos(elevation) * sin(azimuth));
    imageStore(skyViewLut, texel, vec4(skyRadiance(origin, direction, sun), 1.0));
}

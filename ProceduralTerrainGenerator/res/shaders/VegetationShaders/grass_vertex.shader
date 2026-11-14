#version 450 core
//One blade of grass per instance (grass_compute.shader), built from gl_VertexID as a triangle strip: pairs of vertices at SEGMENTS heights
//and the tip. The blade leans and bends in the wind more towards the tip
const int SEGMENTS = 4;

struct Blade {
    vec4 positionHeight;
    vec4 data;
};
layout(std430, binding = 0) readonly buffer Blades { Blade blades[]; };

#include "../common/frame.glsl"

uniform mat4 model;         //modelScale of the view
uniform uint capacity;      //blades in the buffer; the counter of the draw can be larger
uniform vec2 windDirection; //unit XZ
uniform float windStrength; //bend of the tip in blade heights
uniform float windSpeed;

out vec3 FragPos;
out vec3 Normal;
out vec3 Color;
out float Along;            //0 at the base, 1 at the tip

void main()
{
    if (uint(gl_InstanceID) >= capacity) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    Blade blade = blades[gl_InstanceID];
    vec3 base = blade.positionHeight.xyz;
    float height = blade.positionHeight.w;
    float angle = blade.data.x;
    float width = blade.data.y;
    vec4 color = unpackUnorm4x8(floatBitsToUint(blade.data.z));
    float lean = blade.data.w;

    int pair = gl_VertexID / 2;
    bool tip = pair >= SEGMENTS;
    float t = tip ? 1.0 : float(pair) / float(SEGMENTS);
    float side = tip ? 0.0 : (gl_VertexID % 2 == 0 ? -0.5 : 0.5);

    vec3 across = vec3(cos(angle), 0.0, sin(angle));
    vec3 facing = vec3(-across.z, 0.0, across.x);
    //Wind: a wave moving along windDirection plus a faster flutter of each blade
    float phase = dot(base.xz, windDirection) * 0.02 - time * windSpeed;
    float gust = 0.6 + 0.4 * sin(phase) * sin(phase * 0.37 + 1.3);
    float flutter = 0.15 * sin(time * windSpeed * 3.1 + color.a * 6.2831853);
    vec3 wind = vec3(windDirection.x, 0.0, windDirection.y) * windStrength * (gust + flutter);
    vec3 bend = facing * lean + wind;
    //Bent blades get lower, so they keep about their length
    float bendLength = length(bend);
    float upright = 1.0 / sqrt(1.0 + bendLength * bendLength);

    vec3 position = base + vec3(0.0, t * height * upright, 0.0) + bend * (t * t * height) + across * (side * width * (1.0 - t));
    vec3 tangent = vec3(0.0, height * upright, 0.0) + bend * (2.0 * t * height);
    vec3 normal = normalize(cross(across, tangent));
    //Light grass mostly like the ground under it, so the blades do not flicker with their facing
    Normal = normalize(mat3(model) * normalize(mix(normal, vec3(0.0, 1.0, 0.0), 0.6)));
    Color = color.rgb;
    Along = t;
    vec4 world = model * vec4(position, 1.0);
    FragPos = world.xyz;
    gl_Position = projection * view * world;
}

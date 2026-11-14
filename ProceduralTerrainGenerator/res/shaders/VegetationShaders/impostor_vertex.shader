#version 450 core

//Impostor of a model: a quad around the bounding sphere of its variant, facing the camera, drawn as a triangle strip of 4 vertices
//without vertex attributes. The fragment shader blends the four atlas frames around the view direction (VegetationModels::BakeImpostors)

//One placed model per instance, as in vegetation_vertex.shader: xz = base in world units, y = terrain height (times heightScale),
//w = world units per metre of the model; instanceData.x = turn around the vertical axis in radians, .y = variant
layout(location = 0) in vec4 instancePositionScale;
layout(location = 1) in vec4 instanceData;

const int MAX_VARIANTS = 16;    //VegetationModels::maxImpostorVariants

uniform float heightScale;
uniform mat4 model;         //modelScale of the view, uniform in all axes
#include "../common/frame.glsl"
uniform vec3 eye;           //camera position before modelScale
//Per variant: xyz = centre of its bounds in metres of the model, w = radius of the sphere around them; the frames of the atlas show this sphere
uniform vec4 variantSpheres[MAX_VARIANTS];
uniform int frames;         //frames along each side of the atlas

#include "../common/impostor.glsl"

out vec3 FragPos;
out vec2 FrameUV;           //position in a frame, [0, 1]^2
flat out ivec2 frame0;      //lower-left of the four frames blended
flat out vec2 frameBlend;   //weights towards frame0 + (1, 0) and frame0 + (0, 1)
flat out vec2 turnCosSin;
flat out float layer;       //of the atlas = variant

void main()
{
    float c = cos(instanceData.x);
    float s = sin(instanceData.x);
    mat3 turn = mat3(c, 0.0, -s,
                     0.0, 1.0, 0.0,
                     s, 0.0, c);
    int variant = clamp(int(instanceData.y + 0.5), 0, MAX_VARIANTS - 1);
    layer = float(variant);
    vec3 boundsCenter = variantSpheres[variant].xyz;
    float boundsRadius = variantSpheres[variant].w;
    float scale = instancePositionScale.w;
    vec3 base = instancePositionScale.xyz * vec3(1.0, heightScale, 1.0);
    vec3 center = base + turn * (boundsCenter * scale);

    //View direction in the space of the model; from below the horizon the side frames are used
    vec3 toEye = transpose(turn) * normalize(eye - center);
    vec3 direction = normalize(vec3(toEye.x, max(toEye.y, 0.0) + 1e-5, toEye.z));
    vec3 right, up;
    frameAxes(direction, right, up);

    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
    vec3 world = center + turn * (right * corner.x + up * corner.y) * (boundsRadius * scale);

    vec2 grid = hemiOctEncode(direction) * float(frames - 1);
    frame0 = clamp(ivec2(floor(grid)), ivec2(0), ivec2(frames - 2));
    frameBlend = clamp(grid - vec2(frame0), 0.0, 1.0);
    FrameUV = corner * 0.5 + 0.5;
    turnCosSin = vec2(c, s);

    vec4 position = model * vec4(world, 1.0);
    FragPos = position.xyz;
    gl_Position = projection * view * position;
}

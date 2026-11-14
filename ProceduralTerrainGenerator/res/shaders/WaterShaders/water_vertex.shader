#version 450 core
//Water surface over one ground chunk (WaterSurface): a flat quad at waterLevel built from gl_VertexID (triangle strip)

#include "../common/frame.glsl"

uniform mat4 model;         //modelScale of the view
uniform vec2 corner;        //world XZ of the chunk corner, before modelScale
uniform float size;         //side of the chunk in world units
uniform vec2 uvMin;         //height map coordinates of the corner and of the opposite corner
uniform vec2 uvMax;
uniform float waterLevel;   //world units

out vec3 FragPos;           //after modelScale
out vec2 WaterXZ;           //before modelScale, for the waves
out vec2 MapUV;

void main()
{
    vec2 local = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    WaterXZ = corner + local * size;
    MapUV = mix(uvMin, uvMax, local);
    vec4 world = model * vec4(WaterXZ.x, waterLevel, WaterXZ.y, 1.0);
    FragPos = world.xyz;
    gl_Position = projection * view * world;
}

#version 450 core
//Water surface of rivers and lakes (RiverMap, hydrology::BuildWaterMesh): triangles at the water level of each vertex, drawn with
//water_fragment.shader like the sea; the terrain drawn before cuts the shore line through the depth test

#include "../common/frame.glsl"

layout(location = 0) in vec2 aMapPosition;  //map texels
layout(location = 1) in float aLevel;       //terrain height of the surface
layout(location = 2) in vec2 aFlow;         //metres per second along map x, y (= world x, z)

uniform mat4 model;         //modelScale of the view
uniform vec2 mapOrigin;     //world XZ of map position (0, 0), before modelScale
uniform float texelSize;    //world units per map texel
uniform float heightScale;

out vec3 FragPos;           //after modelScale
out vec2 WaterXZ;           //before modelScale, for the waves
out vec2 MapUV;             //unused by mesh water (the depth comes from the copied scene)
out vec2 Flow;

void main()
{
    WaterXZ = mapOrigin + aMapPosition * texelSize;
    MapUV = vec2(0.0);
    Flow = aFlow;
    vec4 world = model * vec4(WaterXZ.x, aLevel * heightScale, WaterXZ.y, 1.0);
    FragPos = world.xyz;
    gl_Position = projection * view * world;
}

#version 450 core
//Water surface of rivers and lakes (RiverMap, hydrology::BuildWaterMesh): triangles at the water level of each vertex, drawn with
//water_fragment.shader like the sea; the terrain drawn before cuts the shore line through the depth test

#include "../common/frame.glsl"

layout(location = 0) in vec2 aMapPosition;  //map texels
layout(location = 1) in float aLevel;       //terrain height of the surface
layout(location = 2) in vec2 aFlow;         //metres per second along map x, y (= world x, z)
layout(location = 3) in float aSlope;       //slope of the river surface, height per map texel
layout(location = 4) in float aFoam;        //foam below a fall upstream (its slope, fading downstream), height per map texel

uniform mat4 model;         //modelScale of the view
uniform vec2 mapOrigin;     //world XZ of map position (0, 0), before modelScale
uniform float texelSize;    //world units per map texel
uniform float heightScale;

out vec3 FragPos;           //after modelScale
out vec2 WaterXZ;           //before modelScale, for the waves
out vec2 MapUV;             //unused by mesh water (the depth comes from the copied scene)
out vec2 Flow;
out float Steep;            //share of falling white water (waterfalls, cascades)
out float FallFoam;         //share of churning foam below a fall

//Slopes of the drawn surface (world units over world units) between which the water turns white, and those of a fall upstream that
//leave foam below it. About 1% of the river cells of the default world are steeper than 0.5 (TerrainGen, height scale 1024)
const vec2 STEEP_SLOPES = vec2(0.3, 0.7);
const vec2 FOAM_SLOPES = vec2(0.15, 0.6);

void main()
{
    WaterXZ = mapOrigin + aMapPosition * texelSize;
    MapUV = vec2(0.0);
    Flow = aFlow;
    //Height per texel to the slope as drawn: the current height scale over the world size of a texel
    float toWorld = heightScale / texelSize;
    Steep = smoothstep(STEEP_SLOPES.x, STEEP_SLOPES.y, aSlope * toWorld);
    FallFoam = smoothstep(FOAM_SLOPES.x, FOAM_SLOPES.y, aFoam * toWorld);
    vec4 world = model * vec4(WaterXZ.x, aLevel * heightScale, WaterXZ.y, 1.0);
    FragPos = world.xyz;
    gl_Position = projection * view * world;
}

#version 450 core
layout(vertices=4) out;

#include "../common/transform.glsl"

in vec2 TexCoord[];
out vec2 TextureCoord[];

uniform float heightScale;
uniform float skirtDepth;

//Largest absolute height of a map (noises are in [-1, 1]) with a margin; heightBound in ChunkWorld.cpp
const float HEIGHT_BOUND = 1.5;

//true if the patch, with any height it can get in the TES, lies entirely outside one plane of the view frustum
bool outsideFrustum()
{
    float top = HEIGHT_BOUND * abs(heightScale);
    float bottom = -top - skirtDepth;
    mat4 clipFromModel = projection * view * model;
    //Corners of the box around the patch in clip space; a point is inside when -w <= x, y, z <= w
    vec4 corners[8];
    for (int i = 0; i < 4; i++) {
        vec4 p = gl_in[i].gl_Position;
        corners[i * 2] = clipFromModel * vec4(p.x, bottom, p.z, 1.0);
        corners[i * 2 + 1] = clipFromModel * vec4(p.x, top, p.z, 1.0);
    }
    for (int axis = 0; axis < 3; axis++) {
        bool belowAll = true, aboveAll = true;
        for (int i = 0; i < 8; i++) {
            belowAll = belowAll && corners[i][axis] < -corners[i].w;
            aboveAll = aboveAll && corners[i][axis] > corners[i].w;
        }
        if (belowAll || aboveAll) {
            return true;
        }
    }
    return false;
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    TextureCoord[gl_InvocationID] = TexCoord[gl_InvocationID];

    if(gl_InvocationID == 0)
    {
        //A tessellation level of 0 discards the patch
        if (outsideFrustum()) {
            gl_TessLevelOuter[0] = 0.0;
            gl_TessLevelOuter[1] = 0.0;
            gl_TessLevelOuter[2] = 0.0;
            gl_TessLevelOuter[3] = 0.0;
            gl_TessLevelInner[0] = 0.0;
            gl_TessLevelInner[1] = 0.0;
            return;
        }

        const int MIN_TESS_LEVEL = 4;
        const int MAX_TESS_LEVEL = 64;
        const float MIN_DISTANCE = 20;
        const float MAX_DISTANCE = 1000;

        vec4 eyeSpacePos00 = view * model * gl_in[0].gl_Position;
        vec4 eyeSpacePos01 = view * model * gl_in[1].gl_Position;
        vec4 eyeSpacePos10 = view * model * gl_in[2].gl_Position;
        vec4 eyeSpacePos11 = view * model * gl_in[3].gl_Position;

        float distance00 = clamp( (abs(eyeSpacePos00.z) - MIN_DISTANCE) / (MAX_DISTANCE-MIN_DISTANCE), 0.0, 1.0 );
        float distance01 = clamp( (abs(eyeSpacePos01.z) - MIN_DISTANCE) / (MAX_DISTANCE-MIN_DISTANCE), 0.0, 1.0 );
        float distance10 = clamp( (abs(eyeSpacePos10.z) - MIN_DISTANCE) / (MAX_DISTANCE-MIN_DISTANCE), 0.0, 1.0 );
        float distance11 = clamp( (abs(eyeSpacePos11.z) - MIN_DISTANCE) / (MAX_DISTANCE-MIN_DISTANCE), 0.0, 1.0 );

        float tessLevel0 = mix( MAX_TESS_LEVEL, MIN_TESS_LEVEL, min(distance10, distance00) );
        float tessLevel1 = mix( MAX_TESS_LEVEL, MIN_TESS_LEVEL, min(distance00, distance01) );
        float tessLevel2 = mix( MAX_TESS_LEVEL, MIN_TESS_LEVEL, min(distance01, distance11) );
        float tessLevel3 = mix( MAX_TESS_LEVEL, MIN_TESS_LEVEL, min(distance11, distance10) );

        gl_TessLevelOuter[0] = tessLevel0;
        gl_TessLevelOuter[1] = tessLevel1;
        gl_TessLevelOuter[2] = tessLevel2;
        gl_TessLevelOuter[3] = tessLevel3;

        gl_TessLevelInner[0] = max(tessLevel1, tessLevel3);
        gl_TessLevelInner[1] = max(tessLevel0, tessLevel2);
    }
}

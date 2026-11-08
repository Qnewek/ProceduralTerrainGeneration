#version 450 core
layout(quads, fractional_odd_spacing, ccw) in;

in vec2 TextureCoord[];

out float Height;
out vec3 FragPos;
out vec3 aColor;
out vec3 aNormal;

uniform sampler2D heightMap;
uniform sampler2D biomeMap;
uniform sampler2D surfaceMap;   //gradient and Laplacian of heightMap per world unit (surface_compute.shader)
uniform int displayMode;
uniform float heightScale;
uniform vec2 texelWorldSize;    //size of one height map texel in world units (x, z), before model
#include "../common/transform.glsl"
#include "../common/surface.glsl"

//Slope shown as fully red in the SLOPE mode, in radians
const float SLOPE_RED = radians(60.0);
//Height difference from the mean of the 4 neighbours (world units) shown as full color in the CURVATURE mode
const float CURVATURE_FULL = 0.25;

void main()
{
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec2 t00 = TextureCoord[0];
    vec2 t01 = TextureCoord[1];
    vec2 t10 = TextureCoord[2];
    vec2 t11 = TextureCoord[3];

    vec2 t0 = (t01 - t00) * u + t00;
    vec2 t1 = (t11 - t10) * u + t10;
    vec2 texCoord = (t1 - t0) * v + t0;

    Height =  texture(heightMap, texCoord).r * heightScale;

    //Map y goes along world +Z
    vec4 derivatives = texture(surfaceMap, texCoord);
    aNormal = normalize(normalMatrix * surfaceNormal(derivatives, heightScale));

    vec4 p00 = gl_in[0].gl_Position;
    vec4 p01 = gl_in[1].gl_Position;
    vec4 p10 = gl_in[2].gl_Position;
    vec4 p11 = gl_in[3].gl_Position;

    vec4 uVec = p01 - p00;
    vec4 vVec = p10 - p00;
    vec4 normal = normalize( vec4(cross(vVec.xyz, uVec.xyz), 0) );

    vec4 p0 = (p01 - p00) * u + p00;
    vec4 p1 = (p11 - p10) * u + p10;
    vec4 p = (p1 - p0) * v + p0 + normal * Height;


    if(displayMode == 0){
        //0 is at 10% brightness; positive values go up to white, negative ones up to 25% brightness with a blue tint
        float h = Height / heightScale;
        if (h >= 0.0)
            aColor = mix(vec3(0.1), vec3(1.0), h);
        else
            aColor = mix(vec3(0.1), vec3(0.20, 0.20, 0.25), -h);
    }
    else if(displayMode == 1){
        float hNorm = clamp((Height + 16.0) / heightScale, 0.0, 1.0);

        if (hNorm < 0.5) {
            aColor = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), hNorm * 2.0);
        } else {
            aColor = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (hNorm - 0.5) * 2.0);
        }
    }
    else if(displayMode == 2){
        aColor = vec3(0.5f, 0.5f, 0.5f);
    }
    else if(displayMode == 3){
        aColor = texture(biomeMap, texCoord).rgb;
    }
    else if(displayMode == 4){
        //Green when flat, yellow at half of SLOPE_RED, red from SLOPE_RED up
        float t = clamp(surfaceSlope(derivatives, heightScale) / SLOPE_RED, 0.0, 1.0);
        if (t < 0.5)
            aColor = mix(vec3(0.15, 0.6, 0.15), vec3(0.95, 0.85, 0.1), t * 2.0);
        else
            aColor = mix(vec3(0.95, 0.85, 0.1), vec3(0.8, 0.1, 0.1), (t - 0.5) * 2.0);
    }
    else if(displayMode == 5){
        //Blue in hollows and valleys, red on hills and ridges, white where the surface is flat or evenly sloped;
        //Laplacian * texel area / 4 = how far the point lies below the mean of its neighbours
        float area = texelWorldSize.x * texelWorldSize.y;
        float t = clamp(surfaceCurvature(derivatives, heightScale) * area / 4.0 / CURVATURE_FULL, -1.0, 1.0);
        if (t >= 0.0)
            aColor = mix(vec3(0.95), vec3(0.15, 0.3, 0.9), t);
        else
            aColor = mix(vec3(0.95), vec3(0.85, 0.15, 0.1), -t);
    }
    //Lighting is done in world space; the 2D map mode only swaps view/projection for a top-down orthographic camera
    vec4 worldPos = model * p;
    FragPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
}
//Camera, sun and fog of the render pass, shared by all shaders (FrameUniforms::Block, field by field)

//The sun: a directional light, the same everywhere
struct Light {
    vec3 direction;     //unit vector towards the sun
    vec3 ambient;
    vec3 diffuse;
};

const int MAX_SHADOW_CASCADES = 4;  //FrameUniforms::maxShadowCascades

layout(std140, binding = 1) uniform FrameBlock {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    float time;         //seconds since the start of the program
    Light light;
    bool lightOn;
    //The surface fades into the sky color between fogStart and fogDistance from the camera
    bool fogOn;
    float fogStart;
    float fogDistance;
    //Sun shadows (common/shadow.glsl): world space -> shadow map [0, 1]^3 per cascade
    mat4 shadowMatrices[MAX_SHADOW_CASCADES];
    vec4 shadowSplits;      //far end of every cascade, as depth along the camera view
    vec4 shadowTexels;      //world size of a shadow map texel of every cascade
    int shadowCascades;     //0 = no shadows
    float shadowStrength;
    //Pass into a depth map (shadows): fragment shaders only test alpha and return
    bool depthPass;
    //Haze (common/fog.glsl): extinction per world unit at y = 0, falling by e every 1 / hazeFalloff world units up; 0 = none
    float hazeDensity;
    float hazeFalloff;
    //Clouds (common/clouds.glsl): x = base, y = top (metres, top <= base = no clouds), z = coverage 0..1, w = strength of their shadows
    vec4 cloudLayer;
    //xy = wind offset of the clouds (metres), z = world units per metre, w = density
    vec4 cloudWind;
};

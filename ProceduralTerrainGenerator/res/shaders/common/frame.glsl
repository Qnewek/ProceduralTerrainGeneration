//Camera, sun and fog of the render pass, shared by all shaders (FrameUniforms::Block, field by field)

//The sun: a directional light, the same everywhere
struct Light {
    vec3 direction;     //unit vector towards the sun
    vec3 ambient;
    vec3 diffuse;
};

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
};

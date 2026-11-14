#version 450 core

//Vertex of a model, in metres with its base at the origin (vegetation::ModelMesh)
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

//One placed model: xz = position of its base in world units (before modelScale), y = terrain height as in the height map
//(times heightScale), w = world units per metre of the model; rotation.x = turn around the vertical axis in radians
struct Instance {
    vec4 positionScale;
    vec4 rotation;
};
layout(std430, binding = 1) readonly buffer Instances {
    Instance instances[];
};

uniform int firstInstance;
uniform float heightScale;  //world units of height 1, as for the terrain
uniform mat4 model;         //modelScale of the view, uniform in all axes
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;

void main()
{
    Instance instance = instances[firstInstance + gl_InstanceID];
    float c = cos(instance.rotation.x);
    float s = sin(instance.rotation.x);
    mat3 turn = mat3(c, 0.0, -s,
                     0.0, 1.0, 0.0,
                     s, 0.0, c);
    vec3 base = instance.positionScale.xyz * vec3(1.0, heightScale, 1.0);
    vec3 world = base + turn * (aPos * instance.positionScale.w);
    vec4 position = model * vec4(world, 1.0);
    FragPos = position.xyz;
    Normal = turn * aNormal;
    UV = aUV;
    gl_Position = projection * view * position;
}

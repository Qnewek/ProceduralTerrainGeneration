//Transformation uniforms shared by all shaders (include after #version); view and projection come from the frame block
uniform mat4 model;
uniform mat3 normalMatrix;      //transpose(inverse(mat3(model)))
#include "frame.glsl"

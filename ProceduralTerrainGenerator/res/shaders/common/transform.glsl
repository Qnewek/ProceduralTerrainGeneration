//Transformation uniforms shared by all shaders (include after #version)
uniform mat4 model;
uniform mat3 normalMatrix;      //transpose(inverse(mat3(model)))
uniform mat4 view;
uniform mat4 projection;

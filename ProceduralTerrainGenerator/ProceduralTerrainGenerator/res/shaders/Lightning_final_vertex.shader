#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aBiome;

out vec3 FragPos;
out vec3 Normal;
out vec3 BiomeColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = aNormal; 
    float biome = aBiome;

    if(biome == 5.0)
    {
        BiomeColor = vec3(0.6, 0.6, 0.55);
    }
	else if(biome == 4.0){
		BiomeColor = vec3(0.5, 0.5, 0.5);
    }
	else if(biome == 3.0){
		BiomeColor = vec3(0.94, 0.89, 0.78);
	}
	else if(biome == 2.0){
		BiomeColor = vec3(0.9, 0.9, 0.9);
	}
    else if(biome == 1.0){
		BiomeColor = vec3(0.65, 0.55, 0.25);
	}
    else if(biome == 0.0){
        BiomeColor = vec3(0.12, 0.52, 0.04);
    }

    gl_Position = projection * view * vec4(FragPos, 1.0);
}
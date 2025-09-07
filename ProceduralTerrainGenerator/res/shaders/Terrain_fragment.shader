#version 450 core

in vec3 FragPos;  
in vec3 Normal;  
in vec3 Color;
in float Height;

out vec4 FragColor;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;    
    float shininess;
}; 

struct Light {
    vec3 position;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform bool lightOn;
uniform bool flatten;
//uniform vec3 viewPos;
uniform Material material;
uniform Light light;

void main()
{
    if(!lightOn){
        FragColor = vec4(Color, 1.0);
        return;
    }

    vec3 baseColor = Color;
    
    vec3 ambient;
    vec3 diffuse;
    vec3 norm = Normal;
    vec3 lightDir = normalize(light.position - FragPos);

    ambient = light.ambient * baseColor.rgb;
    float diff = max(dot(norm, lightDir), 0.0);
    diffuse = light.diffuse * diff * baseColor.rgb;

    //vec3 viewDir = normalize(viewPos - FragPos);
    //vec3 reflectDir = reflect(-lightDir, norm);  
    //float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    //vec3 specular = light.specular * spec * baseColor.rgb;  

    vec3 result = ambient + diffuse;// + specular;

    FragColor = vec4(result, 1.0);
}
    

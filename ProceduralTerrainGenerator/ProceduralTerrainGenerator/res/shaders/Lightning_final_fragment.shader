#version 330 core
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

in vec3 FragPos;  
in vec3 Normal;  
in vec3 BiomeColor;
  
uniform vec3 viewPos;
uniform Material material;
uniform Light light;
uniform sampler2D u_Texture;
uniform float seeLevel;

void main()
{
    vec3 ambient;
    vec3 diffuse;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(light.position - FragPos);
    vec3 color = BiomeColor;

    if(FragPos.y < seeLevel){
        
        color = vec3(0.6, 0.6, 0.55) * FragPos.y/ seeLevel;
    }

    ambient = light.ambient * color;
    float diff = max(dot(norm, lightDir), 0.0);
    diffuse = light.diffuse * diff * color;

    // specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * color * 0.2;  

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
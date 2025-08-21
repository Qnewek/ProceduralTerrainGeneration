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

in float Height;
in vec3 FragPos;  
in vec3 Normal;  
in vec2 TexCoords;
  
uniform vec3 viewPos;
uniform Material material;
uniform Light light;
uniform sampler2D u_Texture;
uniform float scale;

void main()
{
    
    float h = (Height + 16)/scale;
    vec4 color = vec4(h, h, h, 1.0f);

    vec3 ambient;
    vec3 diffuse;
    vec3 norm = Normal;
    vec3 lightDir = normalize(light.position - FragPos);

    ambient = light.ambient * color.rgb;
    float diff = max(dot(norm, lightDir), 0.0);
    diffuse = light.diffuse * diff * color.rgb;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * color.rgb;  

    vec3 result = ambient + diffuse + specular;

    FragColor = vec4(result, 1.0);
}
    

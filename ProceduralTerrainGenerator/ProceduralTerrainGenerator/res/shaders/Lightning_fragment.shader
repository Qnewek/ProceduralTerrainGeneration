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
in vec2 TexCoords;
  
uniform vec3 viewPos;
uniform Material material;
uniform Light light;
uniform sampler2D u_Texture;

void main()
{
    vec3 ambient;
    vec3 diffuse;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(light.position - FragPos);

    if (TexCoords.x < 0.0 && TexCoords.y < 0.0)
    {
        ambient = light.ambient * vec3(0.5,0.5,0.5);
        float diff = max(dot(norm, lightDir), 0.0);
        diffuse = light.diffuse * diff * vec3(0.5,0.5,0.5);
    }
    else
    {
        ambient = light.ambient * texture(u_Texture, TexCoords).rgb;
        float diff = max(dot(norm, lightDir), 0.0);
        diffuse = light.diffuse * diff * texture(u_Texture, TexCoords).rgb;
    }

    // specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * material.specular;  
        
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
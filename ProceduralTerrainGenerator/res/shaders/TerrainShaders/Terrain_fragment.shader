#version 450 core

in vec3 FragPos;
in vec3 aColor;
in vec3 aNormal;
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
uniform Material material;
uniform Light light;

void main()
{ 
    vec3 baseColor = aColor;

    if(!lightOn){
        FragColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 ambient;
    vec3 diffuse;
    vec3 norm = aNormal;
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

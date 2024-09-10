#version 330 core

out vec4 FragColor;

uniform vec4 color;
uniform vec4 lightColor;

void main() {
    FragColor = vec4(color*lightColor);
}
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float gray = texture(texture1, TexCoord).r;
    FragColor = vec4(gray, gray, gray, 1.0);
}
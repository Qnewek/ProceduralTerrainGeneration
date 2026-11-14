#version 450 core
//Clouds over the drawn scene (Clouds::DrawOver), blended as scene * a + rgb (glBlendFunc(GL_ONE, GL_SRC_ALPHA)). Texels of the sky are
//empty (0, 0, 0, 1): the sky composites its own clouds

in vec2 ndc;
out vec4 FragColor;

layout(binding = 13) uniform sampler2D cloudImage;     //Clouds::imageUnit, rgb = light, a = transmittance

void main()
{
    FragColor = texture(cloudImage, ndc * 0.5 + 0.5);
}

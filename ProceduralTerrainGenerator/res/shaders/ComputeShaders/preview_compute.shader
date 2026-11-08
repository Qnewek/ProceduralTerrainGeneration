#version 450 core

// Gray picture of a noise for the UI (NoisePreview): -1 = black, 0 = gray, 1 = white; values outside [-1, 1] are clamped.

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba8, binding = 0) uniform writeonly image2D colors;

uniform sampler2D values;

void main()
{
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, textureSize(values, 0)))) {
		return;
	}
	float gray = clamp(texelFetch(values, p, 0).r * 0.5 + 0.5, 0.0, 1.0);
	imageStore(colors, p, vec4(vec3(gray), 1.0));
}

// Terrain of a Procedural Terrain Generator pack: up to 16 surface materials mixed by the splat maps of the pack
// (4 materials per RGBA file, shares summing to 1), textures repeated every tile_metres in world X and Z.
// Without materials the base color map (the biome colors of the pack) or plain grey.
shader_type spatial;

uniform bool use_materials = false;
uniform int material_count = 0;
// A splat file of the pack could not be read: the share its materials leave is filled with an even mix of all materials
uniform bool splat_missing = false;
uniform sampler2D splat_0 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_1 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_2 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_3 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2DArray albedo_array : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray normal_array : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray roughness_array : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float tile_metres[16];
uniform sampler2D base_color_map : source_color, hint_default_white, filter_linear, repeat_disable;

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	if (use_materials) {
		vec4 shares[4] = vec4[4](texture(splat_0, UV), texture(splat_1, UV), texture(splat_2, UV), texture(splat_3, UV));
		// Derivatives computed once in uniform control flow so skipped layers still sample exact mip levels via textureGrad
		vec2 dpos_dx = dFdx(world_position.xz);
		vec2 dpos_dy = dFdy(world_position.xz);
		vec3 albedo = vec3(0.0);
		vec3 normal_map = vec3(0.0);
		float roughness = 0.0;
		float total = 0.0;
		if (!splat_missing) {
			for (int m = 0; m < material_count; m++) {
				float w = shares[m / 4][m % 4];
				if (w <= 1e-4) {
					continue;
				}
				float inv_tile = 1.0 / max(tile_metres[m], 0.001);
				vec3 uv = vec3(world_position.xz * inv_tile, float(m));
				vec2 dx = dpos_dx * inv_tile;
				vec2 dy = dpos_dy * inv_tile;
				albedo += w * textureGrad(albedo_array, uv, dx, dy).rgb;
				normal_map += w * textureGrad(normal_array, uv, dx, dy).rgb;
				roughness += w * textureGrad(roughness_array, uv, dx, dy).r;
				total += w;
			}
			if (total <= 1e-4 && material_count > 0) {
				float inv_tile = 1.0 / max(tile_metres[0], 0.001);
				vec3 uv = vec3(world_position.xz * inv_tile, 0.0);
				vec2 dx = dpos_dx * inv_tile;
				vec2 dy = dpos_dy * inv_tile;
				albedo = textureGrad(albedo_array, uv, dx, dy).rgb;
				normal_map = textureGrad(normal_array, uv, dx, dy).rgb;
				roughness = textureGrad(roughness_array, uv, dx, dy).r;
				total = 1.0;
			}
		} else {
			vec3 even_albedo = vec3(0.0);
			vec3 even_normal = vec3(0.0);
			float even_roughness = 0.0;
			for (int m = 0; m < material_count; m++) {
				float w = shares[m / 4][m % 4];
				float inv_tile = 1.0 / max(tile_metres[m], 0.001);
				vec3 uv = vec3(world_position.xz * inv_tile, float(m));
				vec2 dx = dpos_dx * inv_tile;
				vec2 dy = dpos_dy * inv_tile;
				vec3 a = textureGrad(albedo_array, uv, dx, dy).rgb;
				vec3 n = textureGrad(normal_array, uv, dx, dy).rgb;
				float r = textureGrad(roughness_array, uv, dx, dy).r;
				albedo += w * a;
				normal_map += w * n;
				roughness += w * r;
				even_albedo += a;
				even_normal += n;
				even_roughness += r;
				total += w;
			}
			float rest = max(1.0 - total, 0.0);
			float count = max(float(material_count), 1.0);
			albedo += rest * even_albedo / count;
			normal_map += rest * even_normal / count;
			roughness += rest * even_roughness / count;
			total = max(total + rest, 1e-4);
		}
		ALBEDO = albedo / max(total, 1e-4);
		NORMAL_MAP = normal_map / max(total, 1e-4);
		ROUGHNESS = roughness / max(total, 1e-4);
	} else {
		ALBEDO = texture(base_color_map, UV).rgb;
		ROUGHNESS = 0.9;
	}
}

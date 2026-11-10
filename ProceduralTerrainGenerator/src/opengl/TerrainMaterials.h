#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include "glm/glm.hpp"

//One surface material of the terrain, as listed in res/textures/terrain_materials.json
struct TerrainMaterial {
	std::string name;
	std::string asset;			//folder in res/textures/terrain/
	float tileSize = 4.0f;		//world units covered by one repeat of the texture
	glm::vec3 fallbackColor = glm::vec3(0.5f);
	int cliffMaterial = -1;		//material that replaces this one on steep slopes; the material itself if none is given
	bool loaded = false;		//false if the textures were missing and the layer holds fallbackColor
};

//Textures of all terrain materials as two texture arrays (one layer per material) and their parameters in a uniform block:
//	unit albedoUnit: RGB = albedo, A = height (for height-based blending)
//	unit detailUnit: RG = normal XY (OpenGL convention, Z rebuilt in the shader), B = roughness, A = ambient occlusion
//	uniform block binding blockBinding: common/materials.glsl
//The arrays and the block are bound once per frame by Bind, so every terrain shader (TerrainView, ChunkWorld) sees them.
//The textures are not in git (tools/fetch_textures.py); a material without them keeps a flat layer of its fallback color.
class TerrainMaterials
{
public:
	TerrainMaterials() = default;
	~TerrainMaterials();
	TerrainMaterials(const TerrainMaterials&) = delete;
	TerrainMaterials& operator=(const TerrainMaterials&) = delete;

	//Reads the material list and loads the textures (decoded in parallel); missing textures are not an error
	//@param manifestPath - res/textures/terrain_materials.json
	//@param textureDir - folder with one subfolder per material asset
	//@return false only if the list cannot be read or is empty
	bool Initialize(const std::string& manifestPath, const std::string& textureDir);

	//Binds the arrays to albedoUnit / detailUnit and the parameters to blockBinding, uploading them if they changed
	void Bind();
	//Section "Terrain materials" in ImGui: loaded textures and display parameters
	void ImGuiDraw();

	const std::vector<TerrainMaterial>& GetMaterials() const { return materials; }

	static constexpr int albedoUnit = 4;
	static constexpr int detailUnit = 5;
	static constexpr GLuint blockBinding = 0;
	//Size of the parameter array in the uniform block (MAX_MATERIALS in materials.glsl)
	static constexpr int maxMaterials = 16;

private:
	//Mirrors the std140 block TerrainMaterialBlock in materials.glsl
	struct Block {
		glm::vec4 params[maxMaterials];		//x = 1 / tileSize, y = cliff material
		int materialCount;
		int previewMaterial;
		float textureScale;
		float normalStrength;
		float cliffSlopeStart, cliffSlopeEnd;	//radians
		float heightInfluence;
		float blendDepth;					//0 = plain crossfade instead of height-based blending
		float triplanarStart, triplanarEnd;	//radians
		float specularStrength;
	};

	void UploadBlock();

	std::vector<TerrainMaterial> materials;
	GLuint albedoArray = 0, detailArray = 0, blockBuffer = 0;
	int layerSize = 0;

	//Display parameters, edited in ImGuiDraw
	int previewMaterial = 0;		//material drawn everywhere in the TEXTURED mode, until biomes choose materials
	float textureScale = 1.0f;		//multiplies tileSize of every material
	float normalStrength = 1.0f;	//0 = flat, 1 = normal maps as authored
	//Slopes (degrees) over which materials give way to their cliff materials: none below the start, all above the end
	float cliffSlopeStart = 32.0f, cliffSlopeEnd = 48.0f;
	//Height-based blending: score of a material = weight + heightInfluence * texture height; materials within blendDepth
	//of the best score are mixed. Off = plain crossfade by the weights
	bool heightBlending = true;
	float heightInfluence = 0.5f;
	float blendDepth = 0.15f;
	//Slopes (degrees) over which the side projections fade in (triplanar); below the start only the projection from above
	float triplanarStart = 20.0f, triplanarEnd = 40.0f;
	float specularStrength = 0.5f;	//0 = no sun highlights
	bool blockOutdated = true;
};

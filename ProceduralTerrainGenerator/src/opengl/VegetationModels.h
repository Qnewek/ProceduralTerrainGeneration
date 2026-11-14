#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <GL/glew.h>

#include "FrameUniforms.h"
#include "Shader.h"
#include "VegetationMesh.h"

class Camera;
class LightSource;

//Vegetation and rock models listed in res/vegetation/vegetation_models.json, ready to draw with instancing.
//Loading runs on worker threads: a model comes from its cache in res/vegetation/cache/ or from its glTF file
//(downloaded by tools/fetch_models.py), whose levels of detail are then built and cached. Update installs finished
//models on the GL thread. Each model has one vertex and index buffer with all its levels and one pair of textures per material:
//	albedo: RGB = albedo, A = alpha (255 without an alpha map)
//	detail: RG = normal XY (OpenGL convention), B = roughness, A = ambient occlusion
class VegetationModels
{
public:
	enum class State { MISSING, LOADING, READY, FAILED };

	//A draw of the indices of one part; instanceCount and baseInstance come from the culling (VegetationLayer)
	struct MeshCommand {
		uint32_t indexCount = 0;
		uint32_t firstIndex = 0;
		int variant = 0;
		int lod = 0;
	};

	struct Model {
		std::string name;
		std::string asset;
		std::string kind;
		std::vector<uint32_t> lodTargets;
		State state = State::LOADING;
		std::string error;
		//Of the mesh, without its vertices and indices (they live in the buffers)
		std::vector<vegetation::Variant> variants;
		std::vector<vegetation::Material> materials;
		bool fromCache = false;
		double loadMs = 0.0;
		GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0;
		std::vector<GLuint> albedo, detail;		//per material
		//Impostor atlases of trees (kind "tree"), GL_TEXTURE_2D_ARRAY with one layer per variant; 0 = none (see BakeImpostors)
		GLuint impostorAlbedo = 0, impostorNormal = 0;
		//Indirect drawing of the meshes: one command per part of every (variant, level), ordered by material (MeshCommand)
		std::vector<MeshCommand> commands;
		std::vector<int> commandMaterial;		//material of each command
		//Commands of each material: [first, first + count) of commands; materials without parts are left out
		struct MaterialRun { int material = 0; int first = 0; int count = 0; };
		std::vector<MaterialRun> runs;
	};
	//Instance as the shaders read it (vertex attributes with divisor 1): (x, height, z, world units per metre), (yaw, variant, model, layer)
	struct GpuInstance {
		glm::vec4 positionScale;
		glm::vec4 data;
	};

	//Defined where Loaded and Preview are complete
	VegetationModels();
	~VegetationModels();
	VegetationModels(const VegetationModels&) = delete;
	VegetationModels& operator=(const VegetationModels&) = delete;

	//Reads the model list and starts loading in the background
	//@param resourceDir - res/vegetation (with vegetation_models.json, models/ and cache/)
	//@return false if the list cannot be read
	bool Initialize(const std::string& resourceDir);
	//Installs models whose loading finished; call every frame on the GL thread
	void Update();
	bool ReloadShader()
	{
		const bool mesh = shader && shader->Reload();
		const bool impostors = impostorShader && impostorShader->Reload();
		return mesh && impostors;
	}

	const std::vector<Model>& GetModels() const { return models; }
	int LoadingCount() const;

	//Binds the shader, uploads the camera, sun and fog of the pass and sets the per-frame uniforms
	//@param model - modelScale of the view
	//@param heightScale - world units of height 1 (the y of the instances is a height of the map)
	void BeginDraw(const FrameUniforms::Block& frame, const glm::mat4& model, float heightScale);
	//Buffer of GpuInstance the draws read their instances from
	void SetInstanceBuffer(GLuint buffer) { instanceBuffer = buffer; }
	//Draws instances [firstInstance, firstInstance + instanceCount) of a level of detail of a variant of a ready model
	void Draw(int model, int variant, int lod, int firstInstance, int instanceCount);
	//Draws all meshes of a model with the model's commands (Model::commands) as DrawElementsIndirectCommand, starting at
	//firstCommand of the buffer bound to GL_DRAW_INDIRECT_BUFFER
	void DrawMeshesIndirect(int model, int firstCommand);

	//Impostors: each variant of a tree rendered from impostorFrames x impostorFrames directions of the upper hemisphere into
	//cells of impostorCell pixels (octahedral mapping, Impostor.h); far away a camera-facing quad blends the four nearest frames
	static constexpr int impostorFrames = 8;
	static constexpr int impostorCell = 128;
	//Variants of a model with impostors at most (MAX_VARIANTS in impostor_vertex.shader)
	static constexpr int maxImpostorVariants = 16;
	bool HasImpostors(int model) const { return models[model].impostorAlbedo != 0; }
	//As BeginDraw, for DrawImpostorsIndirect
	//@param eye - camera position before modelScale (the impostors face it)
	void BeginImpostorDraw(const FrameUniforms::Block& frame, const glm::mat4& model, const glm::vec3& eye, float heightScale);
	//Draws the impostors of a model: one DrawArraysIndirectCommand per variant, starting at firstCommand of the buffer bound to
	//GL_DRAW_INDIRECT_BUFFER (the variant of an instance is in its data); nothing without impostors
	void DrawImpostorsIndirect(int model, int firstCommand);
	//Albedo atlas of a variant (premultiplied RGBA, rows from the bottom up), ImpostorSize() pixels square; empty without impostors
	std::vector<unsigned char> ImpostorAtlas(int model, int variant) const;
	static int ImpostorSize() { return impostorFrames * impostorCell; }

	//"Vegetation" tab: list of models and their levels of detail, preview of the selected model
	void ImGuiDraw(const LightSource& light);

	//Picture of a ready model from the preview camera, PreviewSize() pixels square, RGBA rows from the bottom up
	//@param yaw, pitch - direction of the camera around the model in radians
	std::vector<unsigned char> PreviewImage(int model, int variant, int lod, float yaw, float pitch, bool wireframe, const LightSource& light);
	static int PreviewSize();

private:
	struct Loaded;
	struct Preview;

	//Runs on a worker: cache or glTF, then textures; no OpenGL here
	Loaded Load(size_t index) const;
	void Install(size_t index, Loaded& loaded);
	//Renders the impostor atlases of a just installed tree (its first level of detail); other kinds get none
	void BakeImpostors(size_t index);
	//Preview of the tab: turns the camera and renders the selected model
	void DrawPreview(const LightSource& light);
	//Renders a model into the preview framebuffer
	void RenderPreview(int model, int variant, int lod, float yaw, float pitch, bool wireframe, const LightSource& light);

	std::string resourceDir;
	std::vector<Model> models;
	std::unique_ptr<Shader> shader;
	FrameUniforms frameUniforms;
	std::unique_ptr<Shader> impostorShader;
	GLuint fallbackAlbedo = 0, fallbackDetail = 0;
	//Impostor quads: only the instance attributes (binding 0, divisor 1)
	GLuint impostorVao = 0;
	GLuint instanceBuffer = 0;
	//Framebuffer and depth of impostor baking, made with the first tree
	GLuint bakeFramebuffer = 0, bakeDepth = 0;

	//Background loading: workers take the next model index and leave the result in finished
	std::vector<std::thread> workers;
	std::atomic<size_t> nextJob = 0;
	std::atomic<bool> stopping = false;
	std::mutex finishedMutex;
	std::vector<std::pair<size_t, std::unique_ptr<Loaded>>> finished;

	std::unique_ptr<Preview> preview;
	float normalStrength = 1.0f;
	float specularStrength = 0.3f;
};

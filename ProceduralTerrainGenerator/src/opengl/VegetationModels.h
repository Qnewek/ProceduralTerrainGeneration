#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <GL/glew.h>

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
	bool ReloadShader() { return shader && shader->Reload(); }

	const std::vector<Model>& GetModels() const { return models; }
	int LoadingCount() const;

	//Binds the shader and sets the per-frame uniforms; the instances must be bound to SSBO binding instanceBinding
	//@param model - modelScale of the view
	//@param heightScale - world units of height 1 (the y of the instances is a height of the map)
	void BeginDraw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& viewPos, const glm::mat4& model, float heightScale,
		const LightSource& light, bool fogOn, float fogStart, float fogDistance);
	//Binding of the instance buffer (Instances in vegetation_vertex.shader): two vec4 per instance,
	//(x, height, z, world units per metre) and (yaw, 0, 0, 0)
	static constexpr unsigned int instanceBinding = 1;
	//Draws instances [firstInstance, firstInstance + instanceCount) of a level of detail of a variant of a ready model
	void Draw(int model, int variant, int lod, int firstInstance, int instanceCount);

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
	//Preview of the tab: turns the camera and renders the selected model
	void DrawPreview(const LightSource& light);
	//Renders a model into the preview framebuffer
	void RenderPreview(int model, int variant, int lod, float yaw, float pitch, bool wireframe, const LightSource& light);

	std::string resourceDir;
	std::vector<Model> models;
	std::unique_ptr<Shader> shader;
	GLuint fallbackAlbedo = 0, fallbackDetail = 0;

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

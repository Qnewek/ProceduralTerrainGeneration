#define TINYOBJLOADER_IMPLEMENTATION

#include "utilities.h"

#include <math.h>
#include <fstream>
#include <sstream>

#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ObjLoader/tiny_obj_loader.h"

namespace utilities
{
	//Converts float data to unsigned char image
	//@param data - array of floats to be converted
	//@param image - array of unsigned chars to be filled with data
	//@param width - width of the image
	//@param height - height of the image
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
		for (int i = 0; i < width * height; ++i) {
			image[i] = static_cast<unsigned char>(data[i] * 255.0f);
		}
	}

	//Generates cube layout for openGL to draw
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	void GenCubeLayout(float* vertices, unsigned int* indices, float scalingFactor) {
		float cubeVertices[] = {
			// Front row
			-scalingFactor, -scalingFactor,  scalingFactor,
			 scalingFactor, -scalingFactor,  scalingFactor,
			 scalingFactor,  scalingFactor,  scalingFactor,
			-scalingFactor,  scalingFactor,  scalingFactor,
			// Back row
			-scalingFactor, -scalingFactor, -scalingFactor,
			 scalingFactor, -scalingFactor, -scalingFactor,
			 scalingFactor,  scalingFactor, -scalingFactor,
			-scalingFactor,  scalingFactor, -scalingFactor
		};

		//Cube indices
		unsigned int cubeIndices[] = {
			// Front 
			0, 1, 2, 2, 3, 0,
			// Back
			4, 5, 6, 6, 7, 4,
			// Left
			4, 0, 3, 3, 7, 4,
			// Right
			1, 5, 6, 6, 2, 1,
			// Top
			3, 2, 6, 6, 7, 3,
			// Bottom
			4, 5, 1, 1, 0, 4
		};

		for (int i = 0; i < 24; ++i) {
			vertices[i] = cubeVertices[i];
		}

		for (int i = 0; i < 36; ++i) {
			indices[i] = cubeIndices[i];
		}
	}

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	// 
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param map - noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data

	void parseNoiseIntoVertices(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset] = x / (float)width * scalingFactor;
				vertices[((y * width) + x) * stride + offset + 1] = map[y * width + x] * scalingFactor;
				vertices[((y * width) + x) * stride + offset + 2] = y / (float)height * scalingFactor;
			}
		}
	}

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	//
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	//@param chunkX - width of the chunk
	//@param chunkY - height of the chunk
	//@param map - noise map
	//@param scalingFactor - scaling factor for the vertices
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void parseNoiseChunksIntoVertices(float* vertices, int width, int height, int chunkX, int chunkY, float* map, float scalingFactor, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height * chunkY; y++)
		{
			for (int x = 0; x < width * chunkX; x++)
			{
				vertices[((y * width * chunkX) + x) * stride + offset] = x / (float)chunkX * scalingFactor;
				vertices[((y * width * chunkX) + x) * stride + offset + 1] = map[y * width * chunkX + x];
				vertices[((y * width * chunkX) + x) * stride + offset + 2] = y / (float)chunkY * scalingFactor;
			}
		}
	}

	bool createTiledVertices(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset) {
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return false;
		}

		int index = offset;

		for (int y = 0; y < (height - 1); y++)
		{
			for (int x = 0; x < (width - 1); x++)
			{
				vertices[index] = x * scalingFactor;
				vertices[index + 1] = map[y * width + x] * scalingFactor;
				vertices[index + 2] = y * scalingFactor;

				index += stride;

				vertices[index] = (x + 1) * scalingFactor;
				vertices[index + 1] = map[y * width + x + 1] * scalingFactor;
				vertices[index + 2] = y * scalingFactor;

				index += stride;

				vertices[index] = (x + 1) * scalingFactor;
				vertices[index + 1] = map[(y + 1) * width + x + 1] * scalingFactor;
				vertices[index + 2] = (y + 1) * scalingFactor;

				index += stride;

				vertices[index] = x * scalingFactor;
				vertices[index + 1] = map[(y + 1) * width + x] * scalingFactor;
				vertices[index + 2] = (y + 1) * scalingFactor;

				index += stride;
			}
		}

		return true;
	}

	bool createIndicesTiledField(unsigned int* indices, int width, int height) {
		if (!indices) {
			std::cout << "[ERROR] Indices array not initialized" << std::endl;
			return false;
		}

		int index = 0;

		for (int i = 0; i < (height - 1) * (width - 1); i++) {
			indices[index++] = i * 4;
			indices[index++] = i * 4 + 1;
			indices[index++] = i * 4 + 2;

			indices[index++] = i * 4;
			indices[index++] = i * 4 + 2;
			indices[index++] = i * 4 + 3;
		}

		return true;
	}

	//Generates indices for simple mesh by conesting 4 vertices on the one cell of the grid to form 2 triangles
	//@param indices - array of indices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
	void SimpleMeshIndicies(unsigned int* indices, int width, int height) {
		int index = 0;
		for (int y = 0; y < height - 1; y++) {
			for (int x = 0; x < width - 1; x++) {
				indices[index++] = x + (y * width);
				indices[index++] = x + ((y + 1) * width);
				indices[index++] = x + 1 + (y * width);

				indices[index++] = x + 1 + (y * width);
				indices[index++] = x + ((y + 1) * width);
				indices[index++] = x + 1 + ((y + 1) * width);
			}
		}
	}

	void GenerateTerrainMap(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, unsigned int stride) {
		noise.initMap();
		noise.generateFractalNoiseByChunks();
		parseNoiseChunksIntoVertices(vertices, noise.getWidth(), noise.getHeight(), noise.getChunkWidth(), noise.getChunkHeight(), noise.getMap(), 1.0f / noise.getConfigRef().scale, stride, 0);
		SimpleMeshIndicies(indices, noise.getWidth() * noise.getChunkWidth(), noise.getHeight() * noise.getChunkHeight());
		InitializeNormals(vertices, stride, 3, noise.getHeight() * noise.getChunkHeight() * noise.getWidth() * noise.getChunkWidth());
		CalculateNormals(vertices, indices, stride, 3, (noise.getWidth() * noise.getChunkWidth() - 1) * (noise.getHeight() * noise.getChunkHeight() - 1) * 6);
		NormalizeVector3f(vertices, stride, 3, noise.getWidth() * noise.getChunkWidth() * noise.getHeight() * noise.getChunkHeight());
	}

	//Generates terrain map using Perlin Fractal Noise, transforming it into drawable mesh and
	//also dealing with initialization and calculations of normals for lightning purposes
	//@param noise - Perlin noise object
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param stride - number of floats per vertex
	//@param normals - boolean value to determine if normals should be calculated
	//@param first - boolean value to determine if indices should be generated
	void CreateTerrainMesh(noise::SimplexNoiseClass &noise, float* vertices, unsigned int* indices, float scalingFactor, unsigned int stride, bool normals, bool first)
	{
		noise.generateFractalNoise();
		parseNoiseIntoVertices(vertices, noise.getWidth(), noise.getHeight(), noise.getMap(), scalingFactor, stride, 0);
		if (first)
			SimpleMeshIndicies(indices, noise.getWidth(), noise.getHeight());
		if (normals) {
			InitializeNormals(vertices, stride, 3, noise.getHeight() * noise.getWidth());
			CalculateNormals (vertices, indices, stride, 3, (noise.getWidth() - 1) * (noise.getHeight() - 1) * 6);
			NormalizeVector3f(vertices, stride, 3, noise.getWidth() * noise.getHeight());
		}
	}

	//Performs erosion simulation on the terrain map, updating vertices, indices and normals
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param Track - optional array of floats representing the track of the erosion
	//@param stride - number of floats per vertex
	//@param positionsOffset - offset in the vertex array to start with when filling the data
	//@param normalsOffset - offset in the vertex array to start with when filling the normals
	//@param erosion - erosion object
	void PerformErosion(float* vertices, unsigned int* indices, float scalingFactor, std::optional<float*> Track, int stride, int positionsOffset, int normalsOffset, erosion::Erosion& erosion) {
		erosion.Erode(Track);
		parseNoiseIntoVertices(vertices, erosion.getWidth(), erosion.getHeight(), erosion.getMap(), scalingFactor, stride, positionsOffset);
		InitializeNormals(vertices, stride, normalsOffset, erosion.getHeight() * erosion.getWidth());
		CalculateNormals(vertices, indices, stride, normalsOffset, (erosion.getWidth() - 1) * (erosion.getHeight() - 1) * 6);
		NormalizeVector3f(vertices, stride, normalsOffset, erosion.getWidth() * erosion.getHeight());
	}

	//Generates basic Perlin Fractal Noise and sets coords for texture sampling (painting biome)
	//Its a very basic function, yet could be usefull for some simple terrain generation
	//@param vertices - array of vertices to be filled with data
	//@param map - noise map
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void PaintBiome(float* vertices, float* map, int width, int height, unsigned int stride, unsigned int offset) {
		noise::SimplexNoiseClass* noiseBiome = new noise::SimplexNoiseClass();
		noiseBiome->setMapSize(width, height);
		noiseBiome->initMap();
		noiseBiome->generateFractalNoise();
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset]		= noiseBiome->getMap()[y * width + x] > 0.0f ? noiseBiome->getMap()[y * width + x] : 0.0f;
				vertices[((y * width) + x) * stride + offset + 1] = map	[y * width + x] > 0.0f ? map[y * width + x] : 0.0f;
			}
		}
		delete noiseBiome;
	}
	//The functions sets the coords vertices of texture sampling what can be used as a condition
	//for painting the terrain not based on the texture despite having the texture sampling layout of vertices
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void PaintNotByTexture(float* vertices, int width, int height, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset] = -1.0f;
				vertices[((y * width) + x) * stride + offset + 1] = -1.0f;
			}
		}
	}

	object::Object* loadObj(const std::string& dirPath, const std::string& name)
	{
		tinyobj::ObjReaderConfig reader_config;
		reader_config.mtl_search_path = dirPath;
		tinyobj::ObjReader reader;

		if (!reader.ParseFromFile(dirPath + name + ".obj", reader_config))
		{
			if (!reader.Error().empty())
			{
				std::cerr << "TinyObjReader: " << reader.Error();
			}
			exit(1);
		}
		if (!reader.Warning().empty()) {
			std::cout << "TinyObjReader: " << reader.Warning();
		}

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		auto& materials = reader.GetMaterials();

		object::Object* obj = new object::Object(name);
		obj->asignDirPath(dirPath);
		
		int size = 0;

		for (auto& it : shapes)
		{
			size += it.mesh.indices.size();
		}

		int vertexIndex = 0;
		object::vertex* vertices = new object::vertex[size];
		object::faceTriangle* indices = new object::faceTriangle[size / 3];

		//Loop over shapes
		for (size_t s = 0; s < shapes.size(); s++) {
			// Loop over faces(polygon)
			size_t index_offset = 0;
			for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
				size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

				// Loop over vertices in the face.
				for (size_t v = 0; v < fv; v++) {
					tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

					vertices[vertexIndex] = object::vertex(attrib.vertices[3 * size_t(idx.vertex_index) + 0],
						attrib.vertices[3 * size_t(idx.vertex_index) + 1],
						attrib.vertices[3 * size_t(idx.vertex_index) + 2],
						attrib.normals[3 * size_t(idx.normal_index) + 0],
						attrib.normals[3 * size_t(idx.normal_index) + 1],
						attrib.normals[3 * size_t(idx.normal_index) + 2],
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 1],
						shapes[s].mesh.material_ids[f]);
					vertexIndex++;
				}
				index_offset += fv;
			}
		}

		for (auto& it : materials) {
			obj->addMaterial(object::material(
				it.diffuse_texname,
				it.ambient_texname,
				it.specular_texname,
				{ static_cast<float>(it.ambient[0]), static_cast<float>(it.ambient[1]), static_cast<float>(it.ambient[2]) },
				{ static_cast<float>(it.diffuse[0]), static_cast<float>(it.diffuse[1]), static_cast<float>(it.diffuse[2]) },
				{ static_cast<float>(it.specular[0]), static_cast<float>(it.specular[1]), static_cast<float>(it.specular[2]) },
				static_cast<float>(it.shininess)
			));
		}

		obj->asignVertices(vertices, size);
		obj->asignIndices(indices);

		if (obj->isSpecified())
		{
			std::cout << "[LOG] Object loaded successfully" << std::endl;
		}
		else
		{
			std::cout << "[ERROR] Object couldnt be loaded" << std::endl;
		}

		return obj;
	}

	bool saveToObj(const std::string& dirPath, const std::string& name, float* vertices, unsigned int* indices, unsigned int stride, unsigned int indexSize, unsigned int verticesCount, bool mtl)
	{
		if (!vertices || !indices)
		{
			std::cout << "[ERROR] Arrays not initialized" << std::endl;
			return false;
		}
		if (mtl) {
			std::string mtlfilename = dirPath + name + ".mtl";

			std::ofstream mfile(mtlfilename);
			if (!mfile.is_open()) {
				std::cerr << "Failed to open file: " << mtlfilename << std::endl;
				return false;
			}

			mfile << "newmtl " << "material0" << "\n";
			mfile << "Ka " << 0.6f << " " << 0.6f << " " << 0.6f << "\n";
			mfile << "Kd " << 0.6f << " " << 0.6f << " " << 0.6f << "\n";
			mfile << "Ks " << 0.1f << " " << 0.1f << " " << 0.1f << "\n";
			mfile << "Ns " << 1.0f << "\n";
			mfile << "map_Kd " << "texture.png" << "\n";

			mfile.close();
			std::cout << "Material saved to " << mtlfilename << std::endl;
		}
		std::string filename = dirPath + name + ".obj";

		std::ofstream file(filename);
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return false;
		}
		if (mtl) {
			file << "mtllib " << name + ".mtl" << "\n";
			file << "usemtl material0\n";
		}

		for (int y = 0; y < verticesCount; y += stride) {
				float vx = vertices[y];
				float vy = vertices[y+1];
				float vz = vertices[y+2];
				file << "v " << vx << " " << vy << " " << vz << "\n";
		}
		for (int y = 0; y < verticesCount; y += stride) {
			float vx = vertices[y + 6];
			float vy = vertices[y + 7];
			file << "vt " << vx << " " << vy << "\n";
		}
		for (int y = 0; y < verticesCount; y += stride) {
			float vx = vertices[y + 3];
			float vy = vertices[y + 4];
			float vz = vertices[y + 5];
			file << "vn " << vx << " " << vy << " " << vz << "\n";
		}

		std::cout << "Vertices saved" << std::endl;
		// Write faces
		for (int y = 0; y < indexSize; y += 3) {
			int one = indices[y];
			int two = indices[y + 1];
			int three = indices[y + 2];

			file << "f " << one << "/" << one << "/" << one << " " << two << "/" << two << "/" << two << " " << three << "/" << three << "/" << three << "\n";
		}

		file.close();
		std::cout << "Height map saved to " << filename << std::endl;
	}

	void AssignBiome(float* vertices, int* biomeMap, int width, int height, unsigned int stride, unsigned int offset)
	{
		if (!biomeMap || !vertices)
		{
			std::cout << "[ERROR] Arrays not initialized" << std::endl;
			return;
		}

		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset] = static_cast<float>(biomeMap[y * width + x]);
			}
		}
	}

	void AssignTexturesByBiomes(TerrainGenerator& terraGen, float* vertices, int width, int height, int texAtlasSize, unsigned int stride, unsigned int offset)
	{
		if (!terraGen.getBiomeMap() || !vertices)
		{
			std::cout << "[ERROR] Arrays not initialized" << std::endl;
			return;
		}

		int index = offset;
		int texOffset = 0;

		for (int y = 0; y < (height - 1); y++)
		{
			for (int x = 0; x < (width - 1); x++)
			{
				
				texOffset = terraGen.getBiome(terraGen.getBiomeAt(x, y)).getTexOffset();


				vertices[index] = texOffset % texAtlasSize / static_cast<float>(texAtlasSize);
				vertices[index + 1] = texOffset / texAtlasSize / static_cast<float>(texAtlasSize);

				index += stride;

				vertices[index] = (texOffset % texAtlasSize) / static_cast<float>(texAtlasSize) + (0.99 / texAtlasSize);
				vertices[index + 1] = texOffset / texAtlasSize / static_cast<float>(texAtlasSize);
				index += stride;

				vertices[index] = (texOffset % texAtlasSize) / static_cast<float>(texAtlasSize) + (0.99 / texAtlasSize);
				vertices[index + 1] = (texOffset / texAtlasSize + 1) / static_cast<float>(texAtlasSize);

				index += stride;

				vertices[index] = texOffset % texAtlasSize / static_cast<float>(texAtlasSize);
				vertices[index + 1] = (texOffset / texAtlasSize + 1) / static_cast<float>(texAtlasSize);

				index += stride;
			}
		}
		
	}

	//Initializes normals for the vertices to be 0.0f, its needed for the AddVector3f function
	//@param vertices - array of vertices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param verticesCount - number of vertices
	bool InitializeNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount) {
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return false;
		}
		
		for (int i = 0; i < verticesCount; i++) {
			vertices[i * stride + offSet] = 0.0f;
			vertices[i * stride + offSet + 1] = 0.0f;
			vertices[i * stride + offSet + 2] = 0.0f;
		}

		return true;
	}

	//Calculates normals for the vertices based on the indices of the triangles
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param indexSize - number of indices
	bool CalculateNormals(float* vertices, unsigned int* indices, unsigned int stride, unsigned int offSet, unsigned int indexSize) {
		if (indexSize % 3 != 0) {
			std::cout << "Index size is not a multiple of 3, hence its not a set of triangles" << std::endl;
			return false;
		}
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return false;
		}

		glm::vec3 tmp;
		for (int i = 0; i < indexSize; i += 3) {
			tmp = glm::vec3(
				vertices[indices[i] * stride],
				vertices[indices[i] * stride + 1],
				vertices[indices[i] * stride + 2]);
			tmp = glm::cross(
				glm::vec3(
					vertices[indices[i + 1] * stride],
					vertices[indices[i + 1] * stride + 1],
					vertices[indices[i + 1] * stride + 2]) - tmp,
				glm::vec3(
					vertices[indices[i + 2] * stride],
					vertices[indices[i + 2] * stride + 1],
					vertices[indices[i + 2] * stride + 2]) - tmp);
			AddVector3f(vertices, indices[i] * stride + offSet, tmp);
			AddVector3f(vertices, indices[i + 1] * stride + offSet, tmp);
			AddVector3f(vertices, indices[i + 2] * stride + offSet, tmp);
		}
	}
	//Requires floats in vertices representing a normal vector to be initialized with some value (Func initializeNomals {0.0f, 0.0f, 0.0f)
	//Adds vector3f to the normal vector in the vertices array
	//@param vertices - array of vertices to be filled with data
	//@param index - index of the vertex
	//@param vector3f - vector to be added to the normal vector
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f) {
		vertices[index] += vector3f.x;
		vertices[index + 1] += vector3f.y;
		vertices[index + 2] += vector3f.z;
	}

	//Normalizes the normal vectors in the vertices array
	//@param vertices - array of vertices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param verticesCount - number of vertices
	bool NormalizeVector3f(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount) {
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return false;
		}
		
		glm::vec3 tmp;
		for (int i = 0; i < verticesCount; i++) {
			tmp = glm::vec3(
				vertices[i * stride + offSet],
				vertices[i * stride + offSet + 1],
				vertices[i * stride + offSet + 2]);
			tmp = glm::normalize(tmp);
			vertices[i * stride + offSet] = tmp.x;
			vertices[i * stride + offSet + 1] = tmp.y;
			vertices[i * stride + offSet + 2] = tmp.z;
		}
	}

}



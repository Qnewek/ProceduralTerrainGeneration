#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "glm/glm.hpp"

//Shader program built from source files. Sources may use #include "file" (path relative to the including file).
class Shader
{
private:
	unsigned int m_RendererID;
	//Stage type (GL_VERTEX_SHADER, ...) and source file of every stage, kept for Reload()
	std::vector<std::pair<unsigned int, std::string>> m_Stages;
	std::unordered_map<std::string, int> m_UniformLocationCache;
public:
	Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath);
	Shader(const std::string& filepath);
	Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath, const std::string& tessControlFilepath, const std::string& tessEvaluationFilepath);
	~Shader();

	//Rebuilds the program from the source files. On a compile or link error the current program is kept.
	//@return true if the new program replaced the old one
	bool Reload();

	void Bind() const;
	void Unbind() const;

	//uniforms
	void SetUniform1f(const std::string& name, float value);
	void SetUniform2f(const std::string& name, float v0, float v1);
	void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3);
	void SetUniform1i(const std::string& name, int value);
	void SetUniform3fv(const std::string& name, glm::vec3 v);
	void SetUniformMat4f(const std::string& name, const glm::mat4& matrix);
	void SetUniformMat3f(const std::string& name, const glm::mat3& matrix);

	void SetLightUniforms(glm::vec3 lightPos, glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular);
	void SetMaterialUniforms(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess);
	void SetMVP(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
	void SetViewPos(const glm::vec3& viewPos);
	void SetModel(const glm::mat4& model);
	void SetView(const glm::mat4& view);
	void SetProjection(const glm::mat4& projection);

	unsigned int GetRendererID() const { return m_RendererID; }
private:
	//Reads a source file and expands #include directives recursively
	//@param included - files already pasted into this stage (each file is included once)
	std::string ParseShader(const std::string& filepath, std::unordered_set<std::string>& included);
	unsigned int CompileShader(unsigned int type, const std::string& source, const std::string& filepath);
	static const char* ShaderTypeName(unsigned int type);
	bool CheckLinkStatus(unsigned int program);
	//Compiles and links all stages
	//@return program id, or 0 on any error
	unsigned int BuildProgram();
	int GetUniformLocation(const std::string& name);
};

#pragma once
#include <string>
#include <unordered_map>
#include "glm/glm.hpp"

class Shader
{
private:
	unsigned int m_RendererID;
	std::string m_VertexFilePath;
	std::string m_FragmentFilePath;
	std::unordered_map<std::string, int> m_UniformLocationCache;
public:
	Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath);
	Shader(const std::string& filepath);
	~Shader();

	void Bind() const;
	void Unbind() const;

	//uniforms
	void SetUniform1f(const std::string& name, float value);
	void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3);
	void SetUniform1i(const std::string& name, int value);
	void SetUniform3fv(const std::string& name, glm::vec3 v);
	void SetUniformMat4f(const std::string& name, const glm::mat4& matrix);

	void SetLightUniforms(glm::vec3 lightPos, glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular);
	void SetMaterialUniforms(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess);
	void SetMVP(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
	void SetViewPos(const glm::vec3& viewPos);
	void SetModel(const glm::mat4& model);

	unsigned int GetRendererID() const { return m_RendererID; }
private:
	std::string ParseShader(const std::string& filepath);
	unsigned int CompileShader(unsigned int type, const std::string& source);
	unsigned int CreateShader(const std::string& vertexShader, const std::string& fragmentShader);
	int GetUniformLocation(const std::string& name);
};

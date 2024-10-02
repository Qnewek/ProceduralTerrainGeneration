#include "Shader.h"

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include "Renderer.h"

Shader::Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath)
	: m_VertexFilePath(vertexFilepath), m_FragmentFilePath(fragmentFilepath), m_RendererID(0)
{
	std::string vertexShader = ParseShader(vertexFilepath);
	std::string fragmentShader = ParseShader(fragmentFilepath);
	m_RendererID = CreateShader(vertexShader, fragmentShader);
}

Shader::Shader(const std::string& filepath) {
	std::string vertexShader = ParseShader(filepath + ".vert");
	std::string fragmentShader = ParseShader(filepath + ".frag");
	m_RendererID = CreateShader(vertexShader, fragmentShader);
}

Shader::~Shader()
{
	glDeleteProgram(m_RendererID);
}

unsigned int Shader::CompileShader(unsigned int type, const std::string& source) {
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    GLCALL(glShaderSource(id, 1, &src, nullptr));
    GLCALL(glCompileShader(id));

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        int length;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        char* message = (char*)malloc(length * sizeof(char));
        glGetShaderInfoLog(id, length, &length, message);
        std::cout << "Failed to compile " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << " shader!" << std::endl;
        std::cout << message << std::endl;
        glDeleteShader(id);
        return 0;
    }

    return id;
}

std::string Shader::ParseShader(const std::string& filepath) {
    std::ifstream shaderFile(filepath);
    if (!shaderFile.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }
    std::stringstream shaderStream;
    shaderStream << shaderFile.rdbuf();
    return shaderStream.str();
}

unsigned int Shader::CreateShader(const std::string& vertexShader, const std::string& fragmentShader) {
    unsigned int program = glCreateProgram();
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glValidateProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}

void Shader::Bind() const
{
	GLCALL(glUseProgram(m_RendererID));
}

void Shader::Unbind() const
{
	glUseProgram(0);
}

void Shader::SetUniform1f(const std::string& name, float value)
{
    glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetUniform1i(const std::string& name, int value)
{
	glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetUniformMat4f(const std::string& name, const glm::mat4& matrix)
{
    GLCALL(glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, &matrix[0][0]));
}

void Shader::SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3)
{
	GLCALL(glUniform4f(GetUniformLocation(name), v0, v1, v2, v3));
}
void Shader::SetUniform3fv(const std::string& name, glm::vec3 v) {
    GLCALL(glUniform3fv(GetUniformLocation(name), 1, &v[0]));
}

void Shader::SetLightUniforms(glm::vec3 lightPos, glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular) {
    this->Bind();
    this->SetUniform3fv("light.ambient", ambient);
    this->SetUniform3fv("light.diffuse", diffuse);
    this->SetUniform3fv("light.specular", specular);
    this->SetUniform3fv("light.position", lightPos);
}

void Shader::SetMaterialUniforms(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess) {
	this->Bind();
	this->SetUniform3fv("material.ambient", ambient);
	this->SetUniform3fv("material.diffuse", diffuse);
	this->SetUniform3fv("material.specular", specular);
	this->SetUniform1f("material.shininess", shininess);

}

void Shader::SetMVP(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) {
	this->Bind();
	this->SetUniformMat4f("model", model);
	this->SetUniformMat4f("view", view);
	this->SetUniformMat4f("projection", projection);
}

void Shader::SetViewPos(const glm::vec3& viewPos) {
	this->Bind();
	this->SetUniform3fv("viewPos", viewPos);
}

void Shader::SetModel(const glm::mat4& model) {
	this->Bind();
	this->SetUniformMat4f("model", model);
}

int Shader::GetUniformLocation(const std::string& name)
{
	if(m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
	{
		return m_UniformLocationCache[name];
	}

	GLCALL(int location = glGetUniformLocation(m_RendererID, name.c_str()));
    if(location == -1)
	{
		std::cout << "Warning: uniform '" << name << "' doesn't exist!" << std::endl;
	}

	m_UniformLocationCache[name] = location;
    return location;
}

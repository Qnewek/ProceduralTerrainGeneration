#include "Shader.h"

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include "Renderer.h"

const char* Shader::ShaderTypeName(unsigned int type)
{
    switch (type) {
    case GL_VERTEX_SHADER:          return "vertex";
    case GL_FRAGMENT_SHADER:        return "fragment";
    case GL_TESS_CONTROL_SHADER:    return "tessellation control";
    case GL_TESS_EVALUATION_SHADER: return "tessellation evaluation";
    default:                        return "unknown";
    }
}


Shader::Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath)
	: m_RendererID(0), m_Stages{ {GL_VERTEX_SHADER, vertexFilepath}, {GL_FRAGMENT_SHADER, fragmentFilepath} }
{
	m_RendererID = BuildProgram();
}

Shader::Shader(const std::string& filepath)
	: m_RendererID(0), m_Stages{ {GL_VERTEX_SHADER, filepath + ".vert"}, {GL_FRAGMENT_SHADER, filepath + ".frag"} }
{
	m_RendererID = BuildProgram();
}

Shader::Shader(const std::string& vertexFilepath, const std::string& fragmentFilepath, const std::string& tessControlFilepath, const std::string& tessEvaluationFilepath)
	: m_RendererID(0), m_Stages{ {GL_VERTEX_SHADER, vertexFilepath}, {GL_TESS_CONTROL_SHADER, tessControlFilepath},
		{GL_TESS_EVALUATION_SHADER, tessEvaluationFilepath}, {GL_FRAGMENT_SHADER, fragmentFilepath} }
{
	m_RendererID = BuildProgram();
}

Shader::~Shader()
{
	glDeleteProgram(m_RendererID);
}

bool Shader::Reload()
{
	unsigned int program = BuildProgram();
	if (program == 0) {
		std::cout << "[ERROR] Shader reload failed (" << m_Stages.front().second << "), keeping the previous program" << std::endl;
		return false;
	}
	glDeleteProgram(m_RendererID);
	m_RendererID = program;
	m_UniformLocationCache.clear();
	std::cout << "[LOG] Shader reloaded (" << m_Stages.front().second << ")" << std::endl;
	return true;
}

unsigned int Shader::CompileShader(unsigned int type, const std::string& source, const std::string& filepath) {
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    GLCALL(glShaderSource(id, 1, &src, nullptr));
    GLCALL(glCompileShader(id));

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        int length = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        std::string message(length, '\0');
        glGetShaderInfoLog(id, length, &length, message.data());
        std::cout << "[ERROR] Failed to compile " << ShaderTypeName(type) << " shader: " << filepath << std::endl;
        std::cout << message << std::endl;
        glDeleteShader(id);
        return 0;
    }

    return id;
}

//Checks link status of the program and prints the info log on failure
//@return - true if the program linked successfully
bool Shader::CheckLinkStatus(unsigned int program)
{
    int result;
    glGetProgramiv(program, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string message(length, '\0');
        glGetProgramInfoLog(program, length, &length, message.data());
        std::cout << "[ERROR] Failed to link shader program!" << std::endl;
        std::cout << message << std::endl;
        return false;
    }
    return true;
}

//Reads a shader source and pastes the content of every #include "file" line in its place.
//#line directives keep compiler error line numbers pointing at the original file (the file itself is not named).
std::string Shader::ParseShader(const std::string& filepath, std::unordered_set<std::string>& included) {
    std::ifstream shaderFile(filepath);
    if (!shaderFile.is_open()) {
        std::cout << "[ERROR] Failed to open shader file: " << filepath << std::endl;
        return "";
    }
    included.insert(filepath);

    std::string directory = filepath.substr(0, filepath.find_last_of("/\\") + 1);
    std::stringstream result;
    std::string line;
    int lineNumber = 0;
    while (std::getline(shaderFile, line)) {
        lineNumber++;
        size_t pos = line.find_first_not_of(" \t");
        if (pos != std::string::npos && line.compare(pos, 8, "#include") == 0) {
            size_t open = line.find('"', pos);
            size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
            if (close == std::string::npos) {
                std::cout << "[ERROR] Malformed #include in " << filepath << ":" << lineNumber << std::endl;
                return "";
            }
            std::string includePath = directory + line.substr(open + 1, close - open - 1);
            if (!included.count(includePath)) {
                std::string content = ParseShader(includePath, included);
                if (content.empty()) {
                    return "";
                }
                result << "#line 1\n" << content << "\n";
            }
            result << "#line " << lineNumber + 1 << "\n";
        }
        else {
            result << line << "\n";
        }
    }
    return result.str();
}

unsigned int Shader::BuildProgram()
{
    std::vector<unsigned int> compiled;
    bool ok = true;
    for (const auto& [type, filepath] : m_Stages) {
        std::unordered_set<std::string> included;
        std::string source = ParseShader(filepath, included);
        unsigned int id = source.empty() ? 0 : CompileShader(type, source, filepath);
        if (id == 0) {
            ok = false;
            break;
        }
        compiled.push_back(id);
    }

    unsigned int program = 0;
    if (ok) {
        program = glCreateProgram();
        for (unsigned int id : compiled) {
            glAttachShader(program, id);
        }
        glLinkProgram(program);
        if (CheckLinkStatus(program)) {
            glValidateProgram(program);
        }
        else {
            glDeleteProgram(program);
            program = 0;
        }
    }

    for (unsigned int id : compiled) {
        glDeleteShader(id);
    }
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

void Shader::SetUniform2f(const std::string& name, float v0, float v1)
{
    GLCALL(glUniform2f(GetUniformLocation(name), v0, v1));
}

void Shader::SetUniformMat3f(const std::string& name, const glm::mat3& matrix)
{
    GLCALL(glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, &matrix[0][0]));
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

void Shader::SetView(const glm::mat4& view)
{
    this->Bind();
	this->SetUniformMat4f("view", view);
}

void Shader::SetProjection(const glm::mat4& projection)
{
    this->Bind();
	this->SetUniformMat4f("projection", projection);
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

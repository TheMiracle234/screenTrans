// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "gl/Shader.h"
#include "gl/debug.h"

#include <iostream>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <filesystem>
#include <sstream>
#include "gl/debug.h"

namespace fs = std::filesystem;

static inline std::string readFile(const std::filesystem::path& path) {
    assert(fs::exists(path));
    std::ifstream ifs(path);
    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

//static inline void setUniformLocation(
//    unsigned int shader, std::unordered_map<std::string, int>& uniformLocations, 
//    const char* vs, const char* fs
//) {
//    assert(uniformLocations.empty());
//    {
//        std::stringstream ss(vs);
//        std::string get;
//        while (ss.good()) {
//            ss >> get;
//            if (get == "uniform") {
//                ss >> get;
//                ss >> get;
//                if (get.back() == ';') {
//                    get.pop_back();
//                }
//                int location = glGetUniformLocation(shader, get.c_str());
//                uniformLocations[get] = location;
//            }
//        }
//    }
//    {
//        std::stringstream ss(fs);
//        std::string get;
//        while (ss.good()) {
//            ss >> get;
//            if (get == "uniform") {
//                ss >> get;
//                ss >> get;
//                if (get.back() == ';') {
//                    get.pop_back();
//                }
//                int location = glGetUniformLocation(shader, get.c_str());
//                uniformLocations[get] = location;
//            }
//        }
//    }
//}

namespace gl {

    Shader::Shader(const char* vertexSource, const char* fragmentSource, StrType type) {
        std::string tmp1;
        std::string tmp2;
        if (type == StrType::PATH) {
            tmp1 = readFile(vertexSource);
            tmp2 = readFile(fragmentSource);
            vertexSource = tmp1.c_str();
            fragmentSource = tmp2.c_str();
        }

        GLCALL(unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER));
        glShaderSource(vertexShader, 1, &vertexSource, nullptr);
        glCompileShader(vertexShader);
        // 检查编译错误
        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            std::cerr << "Vertex shader compilation failed:\n" << infoLog << std::endl;
            exit(1);
        }

        // 片段着色器
        unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            std::cerr << "Fragment shader compilation failed:\n" << infoLog << std::endl;
            exit(1);
        }

        // 链接着色器程序
        this->m_id = glCreateProgram();
        glAttachShader(this->m_id, vertexShader);
        glAttachShader(this->m_id, fragmentShader);
        glLinkProgram(this->m_id);
        glGetProgramiv(this->m_id, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(this->m_id, 512, nullptr, infoLog);
            std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
            exit(1);
        }
        // 链接完成后可以删除着色器对象
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    Shader::Shader(Shader&& other) noexcept :
        uniformLocations(std::move(other.uniformLocations)),
        m_id(other.m_id)
    {
        other.m_id = 0;
    }

    void Shader::operator=(Shader&& other)  noexcept {
        if (this == &other)
            return;

        uniformLocations = (std::move(other.uniformLocations));
        m_id = (other.m_id);

        other.m_id = 0;
    }

    Shader::~Shader() {
        GLCALL(glDeleteProgram(m_id));
    }

    void Shader::use() const {
        GLCALL(glUseProgram(m_id));
    }

    int Shader::getLocation(std::string_view name) const {
        auto itr = uniformLocations.find(name);
        if (itr != uniformLocations.end()) { return itr->second; }
        GLCALL(int location = glGetUniformLocation(m_id, name.data()));
        if (location != -1) {
            uniformLocations[std::string(name)] = location;
        }
        else {
            std::cerr << "Warning: Uniform '" << name << "' not found in shader program." << std::endl;
        }
        return location;
    }

    void Shader::setUniform(std::string_view name, bool value)           const {GLCALL(glUniform1i       (getLocation(name), value));}
    void Shader::setUniform(std::string_view name, int value)            const {GLCALL(glUniform1i       (getLocation(name), value));}
    void Shader::setUniform(std::string_view name, float value)          const {GLCALL(glUniform1f       (getLocation(name), value));}
    void Shader::setUniform(std::string_view name, glm::vec3 vec)        const {GLCALL(glUniform3f       (getLocation(name), vec[0], vec[1], vec[2]));}
    void Shader::setUniform(std::string_view name, const glm::mat4& mat) const {GLCALL(glUniformMatrix4fv(getLocation(name), 1, GL_FALSE, glm::value_ptr(mat)));}

}
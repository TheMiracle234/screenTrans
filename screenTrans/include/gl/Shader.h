// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <glad/glad.h>
#include <string_view>
#include <string>
#include <unordered_map>
#include <glm/mat4x4.hpp>

namespace gl {

	struct stringHash {
		using is_transparent = void;
		std::size_t operator()(const std::string& str) const { return std::hash<std::string_view>{}(str); }
		std::size_t operator()(std::string_view str)   const { return std::hash<std::string_view>{}(str); }
		std::size_t operator()(const char* str)        const { return std::hash<std::string_view>{}(str); }
	};

	class Shader
	{
	private:
		mutable std::unordered_map<std::string, int, stringHash, std::equal_to<>> uniformLocations; // since it's just a recorder to accelerate, let's set it as mutable
		int getLocation(std::string_view name) const;
	public:
		enum class StrType {
			PATH,
			SRC
		};
		unsigned int m_id;
		Shader(const char* vertexSource, const char* fragmentSource, StrType type = StrType::SRC);
		~Shader();
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;
		Shader(Shader&&) noexcept;
		void operator=(Shader&&) noexcept;
		void use() const;
		void setUniform(std::string_view name, bool  value) const;
		void setUniform(std::string_view name, int   value) const;
		void setUniform(std::string_view name, float value) const;
		void setUniform(std::string_view name, glm::vec3 vec) const;
		void setUniform(std::string_view name, const glm::mat4& mat) const;
	};

}
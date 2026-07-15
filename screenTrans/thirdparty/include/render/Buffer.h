#pragma once

#include <cstdint>
#include <vector>
#include <memory>

#include <GL/glew.h>

#include "debug/macros.h"

namespace TM {
	
	class Data;
	class Component;

	class Buffer
	{
	TM_public:
		unsigned int VAO, EBO, VBO;

		Buffer() = default;

		//init should always be called by Shader::init
		void init();
		void bind();
		void bindAll();
		static void unbind();

		// set data and enable vertex attrib pointer, the data will be stored in GPU memory, so the data can be released after this function
		void setDataOf(Data& data, unsigned int glDrawType);
		~Buffer();
	};
}

#include "render/Buffer.h"
#include "render/Data.h"
#include "component//Component.h"
#include "debug/debug.h"

#include <iostream>
namespace TM {
	void Buffer::bindAll()
	{
		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	}

	void Buffer::init()
	{
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);

		glGenBuffers(1, &EBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

		glGenBuffers(1, &VBO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
	}

	void Buffer::bind()
	{
		glBindVertexArray(VAO);
	}

	void Buffer::unbind()
	{
		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void Buffer::setDataOf(Data& data, unsigned int glDrawType)
	{
		bindAll();
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * data.indices.size(), data.indices.data(), glDrawType);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * data.vertices.size(), data.vertices.data(), glDrawType);
		auto len = data.structure.index.size();
		for (size_t i = 0; i < len; ++i) {
			glVertexAttribPointer(
				data.structure.index[i], data.structure.attribNum[i], GL_FLOAT, false, data.structure.size, 
				reinterpret_cast<void*>(
					static_cast<uintptr_t>(
						data.structure.startPos[i]))
			);
			glEnableVertexAttribArray(data.structure.index[i]);
		}
	}

	Buffer::~Buffer() {
		glDeleteVertexArrays(1, &VAO);
		glDeleteBuffers(1, &VBO);
		glDeleteBuffers(1, &EBO);
	}
}
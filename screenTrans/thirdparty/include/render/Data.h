#pragma once

#include <vector>
#include <cstdint>
#include <GL/glew.h>
#include "debug/debug.h"

namespace TM {

	struct VertexStructure {
		std::vector<unsigned int> index;
		std::vector<int> attribNum;
		int size;
		std::vector<unsigned int> startPos;
		VertexStructure() = default;

		/// @param _index : indices 
		/// @param _attribNum : how many attributes in a single attribution. e.g. vec2 color is 2
		/// @param _size : total size of a single vertex
		/// @param _startPos : in a single vertex, how many bytes its start pos crosses
		VertexStructure(
			const std::vector<unsigned int>& _index, 
			const std::vector<int>& _attribNum, int _size, 
			const std::vector<unsigned int>& _startPos
		): index(_index), attribNum(_attribNum), size(_size), startPos(_startPos) {
			TM_assertOr(
				index.size() == attribNum.size() && attribNum.size() == startPos.size(),
				"VertexStructure size mismatch"
			);
		}
	};

	class Buffer;
	class Shader;

	class Data
	{
		friend class Buffer;
		friend class Shader;
	TM_private:
		std::vector<float> vertices;
		std::vector<unsigned int> indices;

	TM_public:
		const VertexStructure structure;
		Data() = default;
		explicit Data(const VertexStructure& _structure) :
			structure(_structure) {}

		/// @return The offset of indices
		unsigned int push(const std::vector<float>& _vertices, const std::vector<unsigned int>& _indices);
	};

}
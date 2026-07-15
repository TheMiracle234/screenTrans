#include "render/Data.h"

#include <algorithm>

namespace TM {

	unsigned int Data::push(const std::vector<float>& _vertices, const std::vector<unsigned int>& _indices)
	{
		if (vertices.empty()) {
			vertices.insert(vertices.end(), _vertices.begin(), _vertices.end());
			indices.insert(indices.end(), _indices.begin(), _indices.end());
			return 0;
		}

		unsigned int res = static_cast<unsigned int>(indices.size());

		std::vector<unsigned int> afterIndices = std::vector<unsigned int>(_indices.size());
		unsigned int offset = static_cast<unsigned int>( 
			vertices.size() / (structure.size / sizeof(float)) 
		);
		afterIndices.assign(_indices.begin(), _indices.end());
		std::for_each(afterIndices.begin(), afterIndices.end(),
			[offset](unsigned int& x) {
				x += offset;
			}
		);
		vertices.insert(vertices.end(), _vertices.begin(), _vertices.end());
		indices.insert(indices.end(), afterIndices.begin(), afterIndices.end());

		return res;
	}

}
#pragma once

#include <string>
#include <string_view>
#include <cassert>
#include <iostream>

#include "debug/macros.h"

namespace TM {

	extern std::string lastError;

#	define TM_setError(str) mySetError(str, __FILE__, __LINE__) 

	inline void mySetError(const std::string& str, const std::string& file, int line) {
		lastError = std::string(file) + "\nline " + std::to_string(line) + ": " + str + "\n";

#		ifndef NDEBUG
			TM_perror(lastError);
#		endif
	}

}

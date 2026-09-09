// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#ifndef NDEBUG
#   include <glad/glad.h>
#   include <iostream>
#   include <cassert>
#   define ASSERT(x) if(!(x)) {system("pause");}
#   define GLCALL(x) \
        GLClearError();\
        x;\
        ASSERT(GLLogCall(#x, __FILE__, __LINE__))

    inline void GLClearError() {
        while (glGetError() != GL_NO_ERROR);
    }

    inline bool GLLogCall(const char* function, const char* file, int line) {
        while (GLenum error = glGetError()) {
            std::cerr << "[OpenGL Error](" << error << "):" << function
                << " " << file << ":" << line << std::endl;
            return false;
        }
        return true;
    }
    /* assert but keep expression in release */
#   define GLASSERTK(x) assert(x)
#else
#   define GLASSERTK(x) (void)(x)
#   define GLCALL(x) x
#endif
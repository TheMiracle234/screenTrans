// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include "gl/debug.h"
#include <glm/vec2.hpp>

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

namespace gl {

	struct Viewport {
		int x, y, w, h;
	};

	class Window {
	public:
		int m_width;
		int m_height;
		glm::ivec2 m_pos{0};
		GLFWwindow* m_get = nullptr;
		Viewport m_viewport;
		Window(int width, int height, glm::ivec2 pos, const char* title, GLFWmonitor* monitor = nullptr, GLFWwindow* share = nullptr);
		Window(const Window&) = delete;
		Window(Window&& other) noexcept = delete;
		Window& operator=(const Window&) = delete;
		void operator=(Window&& other) noexcept = delete;
		~Window();

		void setViewport(const Viewport& vp);
		bool shouldClose();
		void updateSize(int w, int h) { m_width = w, m_height = h; }
		void setSize(int w, int h);
		void updatePos(glm::ivec2 pos) { m_pos = pos; }
		void setPos(glm::ivec2 pos);
		void swapBuffers();
	};

	struct GlfwInitGuard {
		GlfwInitGuard();
		~GlfwInitGuard();
	};
}

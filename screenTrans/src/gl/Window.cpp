// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <gl/Window.h>
#include <GLFW/glfw3.h>
#include "gl/debug.h"
#include <cassert>
namespace gl {

	Window::Window(int width, int height, glm::ivec2 pos, const char* title, GLFWmonitor* monitor, GLFWwindow* share) {
		m_get = glfwCreateWindow(width, height, title, monitor, share);
		setPos(pos);
		assert(m_get != nullptr);
		m_width = width;
		m_height = height;
		m_viewport = { 0,0,m_width, m_height };
	}

	Window::~Window() {
		if (m_get) {
			glfwDestroyWindow(m_get);
		}
	}

	void Window::setViewport(const Viewport& vp) {
		m_viewport = vp;
		glViewport(m_viewport.x, m_viewport.y, m_viewport.w, m_viewport.h);
	}

	void Window::setSize(int w, int h) {
		m_width = w;
		m_height = h;
		glfwSetWindowSize(m_get, m_width, m_height);
	}

	void Window::setPos(glm::ivec2 pos) {
		m_pos = pos;
		glfwSetWindowPos(m_get, (int)m_pos.x, (int)m_pos.y);
	}

	bool Window::shouldClose() {
		return glfwWindowShouldClose(m_get);
	}

	void Window::swapBuffers() {
		glfwSwapBuffers(m_get);
	}

	GlfwInitGuard::GlfwInitGuard() { GLASSERTK(glfwInit() == GLFW_TRUE); }
	GlfwInitGuard::~GlfwInitGuard() { glfwTerminate(); }
}
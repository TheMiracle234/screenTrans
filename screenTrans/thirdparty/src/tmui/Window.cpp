#include "window/Window.h"
#include "debug/debug.h"
#include "component/Component.h"
#include "render/Buffer.h"
#include "render/Shader.h"
#include "component/Event.h"
#include "component/Image.h"

#include <queue>
#include <iostream>
#include <algorithm>

namespace TM {

	int Window::countWindows = 0;

	void Window::setSizeCallback(GLFWwindow* window, int w, int h) {
		auto tm_window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		tm_window->width = w;
		tm_window->height = h;
	}

	void Window::refreshCallback(GLFWwindow* window) {
		auto tm_window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		tm_window->_clearWithOutCompClear();
		for (auto& component : tm_window->components) {
			tm_window->draw(component);
		}
		glfwSwapBuffers(tm_window->window.get());
	}

	Window::Window(int w, int h, std::string_view title, uint32_t bk_RGBA):
		window(nullptr, &glfwDestroyWindow), 
		width(w), height(h), lastWidth(w), lastHeight(h), color(colorOf(bk_RGBA)),
		m_lastTimePoint(std::chrono::high_resolution_clock::now())
	{
		TM_println("window construct");
		// glfwinit
		if (countWindows == 0) {
			TM_assertOr(glfwInit(), "GLFW init error");
			TM_println("glfwInit");
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		}

		window.reset(glfwCreateWindow(w, h, title.data(), nullptr, nullptr));
		TM_assertOr(window, "GLFW window creation error");

		/*
		* ================================================
		* 
		*  all the init only once settings
		* 
		* ================================================
		*/
		if (countWindows == 0) {
			// glewinit
			glfwMakeContextCurrent(window.get());
			TM_assertOr(glewInit() == GLEW_OK, "GLEW init error");
			TM_println("glewInit");
			glEnable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // for font render

			// some class static init
			Shader::staticInit();
			Image::staticInit();
		}
		
		// other gl init related to window obj
		glViewport(0, 0, w, h);

		glfwSetWindowUserPointer(window.get(), this);
		glfwSetWindowSizeCallback(window.get(), setSizeCallback);
		glfwSetWindowRefreshCallback(window.get(), refreshCallback);
		glfwSetWindowSizeLimits(window.get(), minWidth, minHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
		glfwSetKeyCallback(window.get(), Event::keyCallBack);
		glfwSetMouseButtonCallback(window.get(), Event::mouseButtonCallback);
		glfwSetCursorPosCallback(window.get(), Event::cursorPostionCallback);

		this->bind();

		countWindows++;
	}

	Window::~Window()
	{
		countWindows--;
		if(countWindows == 0) {
			TM_println("(glew has no destruct for now)\nglfwTerminate");
			glfwTerminate();
		}
		TM_println("window destruction");
	}

	void Window::thingsBeforeDrawLoop() {
		for (auto itr = components.begin(); itr != components.end();) {
			if ((*itr)->hasAncestor()) {
				itr = components.erase(itr);
			}
			else {
				++itr;
			}
		}
	}

	void Window::renderComps() {
		for (auto& component : components) {
			draw(component);
		}
	}

	void Window::clear() {
		if (!(flags[IS_LOOPING])) {
			thingsBeforeDrawLoop();
			flags.set(IS_LOOPING);
		}
		setViewport(0, 0, width, height);
		_clearWithOutCompClear();
	}

	void Window::_clearWithOutCompClear()
	{
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		update();
	}

	void Window::pushComp(Component* const comp) {
		TM_assertOr(comp != nullptr, "component drawn in the window is nullptr"); 
		components.push_back(comp);
	}

	void Window::setSizeRange(int minW, int minH, int maxW, int maxH)
	{
		minWidth = minW;
		minHeight = minH;
		maxWidth = maxW;
		maxHeight = maxH;
		glfwSetWindowSizeLimits(window.get(), minWidth, minHeight, maxWidth, maxHeight);
	}

	std::string Window::getFPS(int8_t presition)
	{
		const int size = 20;
		char str[size];
		sprintf_s(str, size, "%.*f", presition, 1 / deltaTime);
		return str;
	}

	void Window::draw(Component* const component) {

		std::vector<Component*> stack;
		stack.push_back(component);

		while (!stack.empty()) {
			Component* node = stack.back();
			stack.pop_back();

			TM_assertOr(node != nullptr, "component drawn in the window is nullptr");
			TM_assertOr(node->window == this, "component maybe deleted or drawn by mismatched window");
			if (!node->isActive())
				continue;

			Shader* shader = node->getShader().get();
			if (shader) {
				if (lastShader != shader) {
					lastShader = shader;
					shader->bind();
				}
				node->update();
				auto vp = node->getViewport();
				setViewport(vp.x, height - (vp.y + vp.h), vp.w, vp.h);
				shader->draw(node);
			}

			auto& children = node->getChildren();
			for (auto it = children.rbegin(); it != children.rend(); ++it) {
				stack.push_back(it->get());
			}
		}
	}

	void Window::update()
	{
		lastWidth = width;
		lastHeight = height;

		auto now = std::chrono::high_resolution_clock::now();
		deltaTime = std::chrono::duration<double>(now - m_lastTimePoint).count();
		m_lastTimePoint = now;

		if (flags[COMP_ORDER_CHANGED]) {
			std::stable_sort(components.begin(), components.end(),
				[](Component* c1, Component* c2) {
					return c1->priority > c2->priority;
				});
			flags.remove(COMP_ORDER_CHANGED);
		}
	}

}
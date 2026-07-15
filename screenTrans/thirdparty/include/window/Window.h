#pragma once

#ifndef CPP_VERSION
#	ifdef _MSC_VER
#		define CPP_VERSION _MSVC_LANG
#	else 
#		define CPP_VERSION __cplusplus
#	endif
#endif

#if CPP_VERSION < 201703L
#	error "cpp version should be at least C++17. if you wanna use module, it should be as least C++20"
#endif

#undef CPP_VERSION

/// @notice: all the TM_public obj in class or struct is "read only"
///	all the "gl init part" and something "must init after glinit but has to partly init before glinit" should be init in window constructer only once
///
/// the places where // by ÖÐÎÄ were done by ai
///
///
///
///

#include <memory>
#include <string_view>
#include <unordered_set>
#include <chrono>
#include <vector>
#include <cstdint>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/vec4.hpp>

#include "component/Event.h"
#include "debug/macros.h"

#include "util/util.h"

#include "ST_API.h"

namespace TM {

	struct ST_API Viewport {
		int x, y, w, h;
	};

	inline constexpr int NO_LIMIT = GLFW_DONT_CARE;

	class Shader;
	class Component;
	// windows only borrows root components (comp.hasAncestor() == false)
	class ST_API Window
	{
		friend class Component;
	TM_private:
		enum {
			COMP_ORDER_CHANGED = 1,
			IS_LOOPING = 2,
		};
		Flag8 flags; // universal flags
		int minWidth = 200;
		int minHeight = 200;
		int maxWidth = NO_LIMIT;
		int maxHeight = NO_LIMIT;
		std::unique_ptr<GLFWwindow, void(*)(GLFWwindow*)> window;
		std::chrono::steady_clock::time_point m_lastTimePoint;
		glm::vec4 color; // this is stupied but necessary for now. cuz of refreshCallback things
		// just for draw loop. this is a borrow, not an ownership, so no smart ptr
		// if any component is destructed, destructor of Component will remove its borrow
		std::vector<Component*> components;
		Shader* lastShader = nullptr; // save state

	TM_public:
		int width;
		int height;
		int lastWidth;
		int lastHeight;
		double deltaTime = 1.0;

	TM_private:
		void update();
		void _clearWithOutCompClear();
		//void _drawWithoutCompClear(Component * const component);
		void pushComp(Component* const comp);
		void thingsBeforeDrawLoop();
		void setCompOrderChanged() { flags.set(COMP_ORDER_CHANGED); }
		// before swapBuffers, all the comps borrowed will be drawn
		void renderComps();
		template<bool renderInside = false>
		void swapBuffers() {
			if constexpr (renderInside) {
				renderComps();
			}
			glfwSwapBuffers(window.get());
			glfwPollEvents();
			Event::mouseMsg.setOldPos();
		}

	TM_public:
		explicit Window(int w, int h, std::string_view title, uint32_t bk_RGBA = 0x4477FFFF);
		~Window();

		auto& get() { return window; }
		void draw(Component* const component);
		void bind()					{ glfwMakeContextCurrent(window.get()); components.clear(); flags.remove(IS_LOOPING); }
		void clear();
		void close()				{ glfwSetWindowShouldClose(window.get(), true); }
		bool closed()				{ return glfwWindowShouldClose(window.get()); }
		void setSizeRange(int minW, int minH, int maxW, int maxH);
		void setBackColor(uint32_t RGBA)			{ color = colorOf(RGBA); }
		void setBackColor(const glm::vec4& _color)	{ color = _color; }
		std::string getFPS(int8_t presition = 2);

	//static
	TM_public:
		static int countWindows;
		static void setViewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }
		static void setViewport(Viewport vp) { glViewport(vp.x, vp.y, vp.w, vp.h); }

	TM_private:
		static void setSizeCallback(GLFWwindow* window, int w, int h);
		static void refreshCallback(GLFWwindow* window);
	
	TM_public:
		static bool glewiInited() { return countWindows != 0; }
	};
}

#include "Component/Event.h"

namespace TM {

	KeyMsg Event::keyMsg;
	MouseMsg Event::mouseMsg;

// key: 
//     a,b,c,...
// action: only 3:
//     GLFW_PRESS
//     GLFW_RELEASE
//     GLFW_REPEAT
// mods: (only attrib allowed to MOD1 | MOD2)
//     GLFW_MOD_SHIFT
//     ...

	void Event::keyCallBack(GLFWwindow* window, int key, int scancode, int action, int mods) {
		keyMsg(key, scancode, action, mods);
	}

	void Event::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		mouseMsg(button, action, mods);
	}

	void Event::cursorPostionCallback(GLFWwindow* window, double xpos, double ypos) {
		mouseMsg(static_cast<int>(xpos), static_cast<int>(ypos));
	}

}
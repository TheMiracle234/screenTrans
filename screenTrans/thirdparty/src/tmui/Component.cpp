#include "component/Component.h"
#include "render/Shader.h"
#include "debug/debug.h"

#include <iostream>
#include <algorithm>

#include <GL/glew.h>

namespace TM {

	Component::Component(const std::shared_ptr<Shader>& _shader, Window& _window, std::unique_ptr<Component> _child):
		shader(_shader), window(&_window)
	{
		TM_println("Component construct");
		window->pushComp(this);
		flags.set(ACTIVE);
		if(_child){
			pushChild(std::move(_child));
		}
	}

	Component::~Component()
	{
		TM_println("Component destruct");
		this->flags.set(IS_DESTROYING);
		// parent delete it
		if (parent && !(parent->flags[IS_DESTROYING])) {
			return;
			for (int i = 0;i < parent->children.size();++i) {
				if (parent->children[i].get() == this) {
					parent->deletedChildrenIndices.push_back(i);
					break;
				}
			}
		}
		//window delete it
		auto& comps = window->components;
		auto it = std::find(comps.begin(), comps.end(), this);
		if (it != comps.end()) {
			TM_println("erase borrow");
			comps.erase(it);
		}
	}

	bool Component::hasAncestor(Component* const prt)
	{
		if (!prt || !this->parent) {
			return false;
		}

		Component* p = this;
		while (p = p->parent) {
			if (prt == p) {
				return true;
			}
		}

		return false;
	}

	const std::vector<std::unique_ptr<Component>>& Component::getChildren()
	{
		if (!deletedChildrenIndices.empty()) {
			std::sort(deletedChildrenIndices.begin(), deletedChildrenIndices.end(),
				[](int x, int y) {
					return x < y;
				}
			);
			// there is no need to optimize it since it's just few in loops
			do{
				children.erase(children.begin() + deletedChildrenIndices.back());
				deletedChildrenIndices.pop_back();
			} while (!deletedChildrenIndices.empty());
		}
		return children;
	}

}
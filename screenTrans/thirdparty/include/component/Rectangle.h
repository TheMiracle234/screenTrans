#pragma once

#include "component/Component.h"
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

#include "ST_API.h"

namespace TM {

	class ST_API Rectangle : TM_public Component
	{
	TM_protected:
		float width;
		float height;

		glm::vec2 position;
		glm::vec4 rectColor;

	TM_public:
		Rectangle(
			int x, int y, int _width, int _height, Window& _window, 
			uint32_t RGBA = 0x4477FFFF, std::unique_ptr<Component> _child = nullptr, 
			const std::shared_ptr<Shader>& _shader = nullptr, bool loadData = true
		);
		//Rectangle(const Rectangle&) = delete;
		//Rectangle& operator=(const Rectangle&) = delete;
		virtual ~Rectangle();
		void setWidth(int w)	{ width = static_cast<float>(w); }
		void setHeight(int h)	{ height = static_cast<float>(h); }
		void resize(int w, int h);

	// static 
	TM_public:
		static std::shared_ptr<Shader> defaultShader(bool destruct = false);

	// override
	TM_protected:
		void initVerticesIndices() override;
		void initIndicesPos() override;

	TM_public:
		unsigned int getGlDrawMode() const override { return GL_TRIANGLES; }
		glm::vec2 getRelPos() override { return position; }
		void update() override;
	};

}
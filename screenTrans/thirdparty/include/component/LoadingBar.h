#pragma once

#include "component/Component.h"
#include "component/Rectangle.h"
#include "util/util.h"

namespace TM {
	enum class Axis : uint8_t{
		x, y
	};

	class SizableBar : TM_public Component
	{
	TM_protected:
		Axis direction;
		float width;
		float height;
		float currPos; // x or y
		glm::vec2 position;
		glm::vec4 barColor; // on the front

	TM_public:
		SizableBar(
			int x, int y, int _width, int _height, Window& _window, Axis dir,
			uint32_t barRGBA = 0x4477FFFF, std::unique_ptr<Component> _child = nullptr, const std::shared_ptr<Shader>& _shader = nullptr, bool loadData = true
		);

		virtual ~SizableBar();

		void setWidth(int w) { width = static_cast<float>(w); }
		void setHeight(int h) { height = static_cast<float>(h); }
		// absolute pos in window, and it would be clamped in the range of the bar
		void setCurrAbsPos(float pos) { 
			if (direction == Axis::y) {
				currPos = clamp(pos, position.y, position.y + height);
			}
			else {
				currPos = clamp(pos, position.x, position.x + width);
			}
		}
		// ratio: 0.0 ~ 1.0
		void setCurrRelPos(float ratio) { 
			if (direction == Axis::y) {
				setCurrAbsPos(position.y + ratio * height); 
			}
			else {
				setCurrAbsPos(position.x + ratio * width);
			}
		}

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


	// first child is always the Rectangle, which is the bk
	// second child is always the SizableBar, which is the front
	class LoadingBar : TM_public Component{
	TM_private:
		static constexpr int rectIdx = 0;
		static constexpr int barIdx = 1;

	TM_public:
		LoadingBar(
			int x, int y, int _width, int _height, Window& _window, Axis dir, uint32_t barRGBA = 0x4477FFFF, uint32_t bkColor = 0x55555555, 
			std::unique_ptr<Component> _child = nullptr, const std::shared_ptr<Shader>& _barShader = nullptr, const std::shared_ptr<Shader>& _rectShader = nullptr, 
			bool loadData = true
		);

		void setWidth(int w);
		void setHeight(int h);
		void setSize(int w, int h);
		// absolute pos in window, and it would be clamped in the range of the bar
		void setCurrAbsPos(float pos);
		// ratio: 0.0 ~ 1.0
		void setCurrRelPos(float ratio);


		// override
	TM_protected:
		void initVerticesIndices() override {}
		void initIndicesPos() override {}

	TM_public:
		unsigned int getGlDrawMode() const override { return GL_TRIANGLES; }
		glm::vec2 getRelPos() override { return getChildren()[barIdx]->getRelPos(); }
		void update() override {}
	};
}
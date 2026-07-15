#pragma once

#include "component/Rectangle.h"
#include <string>
#include <string_view>
#include <cstdint>

#include <glm/vec4.hpp>

namespace TM {

	inline constexpr const char* DEFAULT_FONT_PATH = "fonts/msyh.ttc";

	class Text : TM_public Rectangle {
	TM_protected:
		FT_Face face;
		glm::vec4 txtColor;
		std::string txt;

	TM_public:
		Text(
			Window& window, int x, int y, int size, std::string_view _u8str, uint32_t _txtRGBA = 0x000000FF, uint32_t _bkRGBA = 0,
			std::string_view fontPath = DEFAULT_FONT_PATH, const std::shared_ptr<Shader>& _shader = nullptr, int h = 0
		);

		virtual ~Text();

		void reset(std::string_view u8str) { txt = u8str; }
		int getTextWidth() { 
			if (width) { return static_cast<int>(width); }
			return shader->renderText(
				getFace().value(), getText().value(), getTextPos(), getFontWidth(), getFontHeight(), 1.0f,
				getTextColor().value(), shader->id, shader->getVAO(), *window, getViewport()
			);
		}

	// override
	TM_public:
		// relative pos in viewport
		glm::vec2 getTextPos() override { return getRelPos(); }
		std::optional<std::string_view> getText() override { return txt; }
		std::optional<FT_Face> getFace() override { return face; }
		std::optional<glm::vec4> getTextColor() override { return txtColor; }
		int getFontWidth() override { return static_cast<int>(height); }
		int getFontHeight() override { return static_cast<int>(height); }
	};
}


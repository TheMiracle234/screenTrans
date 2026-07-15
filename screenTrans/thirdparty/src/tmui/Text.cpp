#include "component/Text.h"
#include "util/util.h"
#include "render/Shader.h"

namespace TM {
	Text::Text(Window& window, int x, int y, int size, std::string_view _u8str, uint32_t _txtRGBA, uint32_t _bkRGBA,
		std::string_view fontPath, const std::shared_ptr<Shader>& _shader, int h):
		Rectangle(x,y, h, size, window, _bkRGBA, nullptr, _shader, false), txt(_u8str), txtColor(colorOf(_txtRGBA))
	{
        TM_assertOr(!FT_New_Face(Shader::sv.ft, fontPath.data(), 0, &face), "Failed to load font!");
        FT_Set_Pixel_Sizes(face, static_cast<unsigned int>(height), static_cast<unsigned int>(height)); // 设置字体大小
		width = static_cast<float>(getTextWidth());
		TM_COMP_INIT;
	}
	Text::~Text()
	{
		TM_assertOr(!FT_Done_Face(face), "Failed to free font face!");
	}
}
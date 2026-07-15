#include "component/Container.h"
#include "component/Rectangle.h"

namespace TM {
	
	Container::Container(Window& window, int x, int y, int w, int h, uint32_t bkRGBA, std::unique_ptr<Component> _child):
		Component(nullptr, window, std::move(_child)),
		pos(
			static_cast<float>(x) / window.width * 2.0f - 1.0f, 
			static_cast<float>(y) / window.height * 2.0f - 1.0f
		),
		width(static_cast<float>(w) / window.width * 2.0f), 
		height(static_cast<float>(h) / window.height * 2.0f)
	{
		constexpr int aBigLen = 10000; // bigger than a screen size and smaller than max of uint16_t
		auto bk = std::make_unique<Rectangle>(0, 0, aBigLen, aBigLen, window, bkRGBA);
		this->pushChild(std::move(bk));
	}

	const Viewport Container::getViewport()
	{
		return {
			static_cast<int>((pos.x + 1.0f) * window->width / 2.0f),
			static_cast<int>((pos.y + 1.0f) * window->height / 2.0f),
			static_cast<int>(width * window->width / 2.0f),
			static_cast<int>(height * window->height / 2.0f)
		};
	}

}
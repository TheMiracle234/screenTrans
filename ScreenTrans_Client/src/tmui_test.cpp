#include <window/Window.h>
#include <component/Image.h>
#include <component/Rectangle.h>

#include <memory>

int main() {
	TM::Window window(800, 600, "test");
	auto img1 = std::make_unique<TM::Image>(window,0, 0, "image/divid.jpg", 800, 600);
	//auto img2 = std::make_unique<TM::Image>(window, 400, 0, "image/girls.jpg", 400, 600);
	//auto rect = std::make_unique<TM::Rectangle>(0, 0, 400, 600, window);
	while (!window.closed()) {
		//rect->resize(window.width/2, window.height/2);
	}
}
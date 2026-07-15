#include "component/loadingBar.h"
#include "window/Window.h"
#include "render/Data.h"
#include "util/util.h"

#include <glm/gtc/matrix_transform.hpp>

namespace TM {

	SizableBar::SizableBar(
		int x, int y, int _width, int _height, Window& _window, Axis dir,
		uint32_t barRGBA, std::unique_ptr<Component> _child , const std::shared_ptr<Shader>& _shader , bool loadData 
	) :
		Component(_shader ? _shader : defaultShader(), _window, std::move(_child)),
		direction(dir),
		width(static_cast<float>(_width)), height(static_cast<float>(_height)),
		currPos(dir == Axis::x ? static_cast<float>(x) : static_cast<float>(y)),
		position(static_cast<float>(x), static_cast<float>(y)), barColor(colorOf(barRGBA))
	{
		if (loadData) {
			TM_COMP_INIT;
		}
	}

	SizableBar::~SizableBar() {
		defaultShader(true);
	}

	std::shared_ptr<Shader> SizableBar::defaultShader(bool destruct) {
		static std::shared_ptr<Shader> shader;
		if (destruct && shader && shader.use_count() == 2) {
			shader.reset();
			return nullptr;
		}
		if (!shader) {
			const Data _data = Data(
				VertexStructure(
					{ 0 },
					{ 2 },
					2 * sizeof(float),
					{ 0 }
				)
			);

			const std::string_view _vsrc = R"(
				#version 330 core
				layout (location = 0) in vec2 aPos;
				out vec2 vPos;
				uniform mat4 projection;
				void main(){
					vPos = aPos;
					gl_Position = projection * vec4(aPos.x, aPos.y, 0.0f, 1.0f);
				}
			)";

			const std::string_view _fsrc = R"(
				#version 330 core
				uniform vec4 uColor;
				uniform float currPos;
				uniform bool isXDir;
				in vec2 vPos;
				out vec4 FragColor;
				void main(){
					if(isXDir) {
						if(vPos.x > currPos){
							discard;
						}
					} else {
						if(vPos.y > currPos){
							discard;
						}
					}
					FragColor = uColor;
				}
			)";

			shader = std::make_shared<Shader>(_data, _vsrc, _fsrc, GL_STATIC_DRAW);
		}
		return shader;
	}

	void SizableBar::initVerticesIndices() {
		indicesPos.offset = getData().push(
			{
				position.x + width, position.y + height,
				position.x,			position.y + height,
				position.x + width,	position.y,
				position.x,			position.y,
			},
			{
				0, 1, 2,
				1, 2, 3
			}
		);
	}

	void SizableBar::initIndicesPos() {
		indicesPos.count = 6;
	}

	void SizableBar::update() {
		int isXDir = direction == Axis::x;
		auto vp = getViewport();
		glm::mat4 projection = glm::ortho(0.0f, (float)vp.w, (float)vp.h, 0.0f);
		shader->setUniform("uColor", &barColor);
		shader->setUniform("currPos", &currPos);
		shader->setUniform("isXDir", &isXDir);
		shader->setUniform("projection", &projection);
	}


	LoadingBar::LoadingBar(
		int x, int y, int _width, int _height, Window& _window, Axis dir, uint32_t barRGBA, uint32_t bkColor,
		std::unique_ptr<Component> _child, const std::shared_ptr<Shader>& _barShader, const std::shared_ptr<Shader>& _rectShader, bool loadData
	) :
		Component(nullptr, _window, std::move(_child))
	{
		auto rect = std::make_unique<Rectangle>(x, y, _width, _height, _window, bkColor, nullptr, _rectShader);
		auto bar = std::make_unique<SizableBar>(x, y, _width, _height, _window, dir, barRGBA, nullptr, _barShader);
		auto addRect = this->pushChild(std::move(rect));
		auto addBar = this->pushChild(std::move(bar));
		auto& kids = getChildren();
		TM_assertOr((void*)addRect == (void*)kids[rectIdx].get(), "LoadingBar rectIdx not match");
		TM_assertOr((void*)addBar == (void*)kids[barIdx].get(), "LoadingBar barIdx not match");
	}

	void LoadingBar::setWidth(int w) {
		auto& kids = getChildren();
		auto rect = dynamic_cast<Rectangle*>(kids[rectIdx].get());
		auto bar = dynamic_cast<SizableBar*>(kids[barIdx].get());
		rect->setWidth(w);
		bar->setWidth(w);
	}

	void LoadingBar::setHeight(int h) {
		auto& kids = getChildren();
		auto rect = dynamic_cast<Rectangle*>(kids[rectIdx].get());
		auto bar = dynamic_cast<SizableBar*>(kids[barIdx].get());
		rect->setHeight(h);
		bar->setHeight(h);
	}

	void LoadingBar::setSize(int w, int h)
	{
		auto& kids = getChildren();
		auto rect = dynamic_cast<Rectangle*>(kids[rectIdx].get());
		auto bar = dynamic_cast<SizableBar*>(kids[barIdx].get());
		rect->setWidth(w);
		bar->setWidth(w);
		rect->setHeight(h);
		bar->setHeight(h);
	}

	void LoadingBar::setCurrAbsPos(float pos) {
		auto& kids = getChildren();
		auto bar = dynamic_cast<SizableBar*>(kids[barIdx].get());
		bar->setCurrAbsPos(pos);
	}

	void LoadingBar::setCurrRelPos(float ratio) {
		auto& kids = getChildren();
		auto bar = dynamic_cast<SizableBar*>(kids[barIdx].get());
		bar->setCurrRelPos(ratio);
	}

}
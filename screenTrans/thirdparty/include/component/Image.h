#pragma once
#include "component/Component.h"
#include <string_view>
#include <vector>

#include "ST_API.h"

namespace TM {

	class ST_API Image: TM_public Component
	{
	TM_protected:
		unsigned int texture;
		float imgWidth;
		float imgHeight;
		float width;
		float height;
		glm::vec2 position;

	TM_public:
		Image(
			Window& _window, int x, int y, std::string_view u8Path, int _width = DEFAULT_WIDTH, int _height = DEFAULT_HEIGHT,
			std::unique_ptr<Component> _child = nullptr, const std::shared_ptr<Shader>& _shader = nullptr, bool loadData = true
		);
		Image(
			Window& _window, int x, int y, const std::vector<uint8_t>& _data, int imgW, int imgH, int _width = DEFAULT_WIDTH, int _height = DEFAULT_HEIGHT,
			std::unique_ptr<Component> _child = nullptr, const std::shared_ptr<Shader>& _shader = nullptr, bool loadData = true
		);
		//Image(const Image&) = delete;
		//Image& operator=(const Image&) = delete;

		void resetData(const std::vector<uint8_t>& data, int imgW, int imgH);
		void resetData(const std::vector<uint8_t>& data);

		virtual ~Image();

		void resize(int w, int h);
		void relocate(int x, int y);
		void resize_relocate(int w, int h, int x, int y);

		void setWidth(int w) { resize(w, (int)height); }
		void setHeight(int h) { resize((int)width, h); }

		// static 
	TM_public:
		static std::shared_ptr<Shader> defaultShader(bool destruct = false);
		static void staticInit();

		// override
	TM_protected:
		void initVerticesIndices() override;
		void initIndicesPos() override;

	TM_public:
		unsigned int getGlDrawMode() const override { return GL_TRIANGLES; }
		glm::vec2 getRelPos() override { return position; }
		void update() override;
		int getTexture() const { return texture; }
	};

}
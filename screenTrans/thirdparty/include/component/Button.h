#pragma once

#include "component/Event.h"
#include "component/Text.h"
#include "render/Shader.h"
#include "debug/macros.h"
#include <cstdint>
#include <memory>

namespace TM{

	enum class BtnState : uint8_t {
		ON,
		DOWN,
		NONE,
	};

	enum class BtnLayer : uint8_t {
		BACK,	//back
		FRONT,	//texts
	};

	class Button: TM_public Text
	{
	TM_public:
		BtnState state = BtnState::NONE;
	
	TM_private:	
		bool ok = false;
		glm::vec4 bkColors[2]; // ON and DOWN (NONE is from Rectangle)
		glm::vec4 ftColors[3]; 
		BtnState lastState = BtnState::NONE;

		using Rect = Rectangle;

	TM_private:
		void renewFrontColor(glm::vec4 color) { txtColor = color; }

		bool mouseOn() {
			auto pos = Event::mouseMsg.getPos();
			auto realPos = getAbsPos();
			return 
				pos.x >= realPos.x && pos.x <= realPos.x + width &&
				pos.y >= realPos.y && pos.y <= realPos.y + height;
		}

		bool mouseDown() {
			if ((lastState == BtnState::ON || lastState == BtnState::DOWN) && (Event::mouseMsg.action == Action::PRESS || Event::mouseMsg.action == Action::REPEAT))
				return true;
			return false;
		}

	TM_public:
		Button(Window& window, int x, int y, int w, int h, std::string_view _u8str, std::string_view fontPath = DEFAULT_FONT_PATH, 
			uint32_t colorBtNone = 0x22FF44AA, uint32_t colorBtOn = 0xFFFFFFAA, uint32_t colorBtDown = 0xFF0000AA,
			uint32_t colorFtNone = 0x000000AA, uint32_t colorFtOn = 0x99999999, uint32_t colorFtDown = 0x666666AA
		);
		virtual ~Button();

		void setColorOf(glm::vec4 color, BtnState st, BtnLayer layer) { colorOf(st, layer) = color; }

		glm::vec4& colorOf(BtnState st, BtnLayer layer) {
			switch (layer) {
			case BtnLayer::BACK:
				switch (st) {
				case BtnState::NONE:	return rectColor;
				default:			return bkColors[static_cast<uint8_t>(st)];
				}
				break;
			case BtnLayer::FRONT:
				return ftColors[static_cast<uint8_t>(st)];
			}
			return rectColor;
		}

		BtnState getState() { return state; }
		bool pressed() { return state == BtnState::ON && lastState == BtnState::DOWN; }

	// static
	TM_public:
		static std::shared_ptr<Shader> btnDefaultShader(bool destruct = false);

	// override 
	TM_public:
		void update() override;
	};


	// only {none, on, down==none} state
	// no alpha
	class LiteBtn : TM_public Text 
	{
	TM_public:
		BtnState state = BtnState::NONE;
	
	TM_private:
		bool ok = false;
		glm::vec4 bkColor; // ON (NONE is from Rectangle)
		BtnState lastState = BtnState::NONE;

		using Rect = Rectangle;

	TM_private:
		bool mouseOn() {
			auto pos = Event::mouseMsg.getPos();
			auto realPos = getAbsPos();
			return
				pos.x >= realPos.x && pos.x <= realPos.x + width &&
				pos.y >= realPos.y && pos.y <= realPos.y + height;
		}

		bool mouseDown() {
			if ((lastState == BtnState::ON || lastState == BtnState::DOWN) && (Event::mouseMsg.action == Action::PRESS || Event::mouseMsg.action == Action::REPEAT))
				return true;
			return false;
		}

	TM_public:
		LiteBtn(Window& window, int x, int y, int w, int h, std::string_view _u8str, std::string_view fontPath = "fonts/msyh.ttc",
			uint32_t colorBtNone = 0x22FF44AA, uint32_t colorBtOn = 0xFF0000AA,
			uint32_t colorFtNone = 0x000000AA, uint32_t colorFtOn = 0x666666AA
		);
		virtual ~LiteBtn();

		void setColorOf(glm::vec4 color, BtnState st, BtnLayer layer) { colorOf(st, layer) = color; }

		glm::vec4& colorOf(BtnState st, BtnLayer layer) {
			switch (layer) {
			case BtnLayer::BACK:
				switch (st) {
				case BtnState::ON:	return bkColor;
				default:			return rectColor;
				}
				break;
			case BtnLayer::FRONT:
				return Text::txtColor;
			}
			return rectColor;
		}

		BtnState getState() { return state; }
		bool pressed() { return state == BtnState::ON && lastState == BtnState::DOWN; }

		// static
	TM_public:
		static std::shared_ptr<Shader> btnDefaultShader(bool destruct = false) { return Button::btnDefaultShader(); }

		// override 
	TM_public:
		void update() override;
	};

}
// imgui_comp.h
#pragma once
#include "imgui.h"
#include "imgui_internal.h"   // ImLerp / ImClamp / ImMax / ImFabs

namespace imgui_ex {

    struct BrowserButtonStyle
    {
        float   rounding = 8.0f;                  // 圆角半径，胶囊按钮可设为 height/2
        ImVec2  padding = ImVec2(16.0f, 8.0f);   // 文字内边距
        float   borderWidth = 1.0f;
        float   hoverSpeed = 14.0f;                 // hover 过渡速度（越大越快）

        ImU32   bgNormal = IM_COL32(255, 255, 255, 255);
        ImU32   bgHovered = IM_COL32(244, 245, 247, 255);
        ImU32   bgActive = IM_COL32(230, 232, 236, 255);
        ImU32   borderColor = IM_COL32(0, 0, 0, 34);   // ~13% 黑
        ImU32   textColor = IM_COL32(32, 33, 36, 255);
        ImU32   focusColor = IM_COL32(26, 115, 232, 140);   // 键盘焦点外圈
        ImU32   shadowColor = IM_COL32(0, 0, 0, 40);
        bool    shadow = true;
    };

    // size 传 (0,0) 表示根据文字自动撑开；enabled=false 时置灰且不可交互
    bool BrowserButton(const char* label,
        const ImVec2& size = ImVec2(0, 0),
        bool enabled = true,
        const BrowserButtonStyle& style = BrowserButtonStyle());

}
#pragma once
#include "imgui.h"

namespace imgui_ex {

    struct TitleBarStyle
    {
        // ---- 尺寸 ----
        float   height = 36.0f;   // 标题栏视觉高度
        float   titlePaddingX = 14.0f;   // 标题文字左边距
        float   contentGapY = 10.0f;   // 标题栏与内容之间额外间距

        // ---- 背景：现代深灰蓝，激活时更暗、失活时略亮 ----
        ImU32   bgActive = IM_COL32(30, 30, 34, 255);
        ImU32   bgInactive = IM_COL32(45, 45, 50, 255);

        // ---- 标题文字 ----
        ImU32   textActive = IM_COL32(240, 240, 244, 255);
        ImU32   textInactive = IM_COL32(140, 140, 148, 255);

        // ---- 关闭按钮：圆形 + hover 时才浮出柔和红色背景 ----
        ImU32   closeHoverBg = IM_COL32(232, 68, 68, 255);
        float   closeButtonSize = 22.0f;   // 圆形背景直径
        float   closeIconSize = 9.0f;    // X 图标宽高
        float   closeIconThick = 1.4f;    // X 线条粗细

        // ---- 分隔线 ----
        ImU32   separator = IM_COL32(255, 255, 255, 16);
        bool    showSeparator = true;

        // ---- 开关 ----
        bool    showClose = true;

        // ---- 动画速度 ----
        float   hoverSpeed = 14.0f;
    };

    // 与 ImGui::Begin 用法一致：
    //   if (imgui_ex::BeginWithTitleBar("窗口##id", &open)) { ... }
    //   ImGui::End();
    bool BeginWithTitleBar(const char* name,
        bool* p_open = nullptr,
        ImGuiWindowFlags flags = 0,
        const TitleBarStyle& tb = TitleBarStyle());

} // namespace imgui_ex
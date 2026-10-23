#include "imgui_ex/TitleBar.hpp"
#include "imgui_internal.h"   // ImClamp / ImLerp / ImFabs / StartMouseMovingWindow

#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18300
#  define IMGUI_EX_ROUND_TOP ImDrawFlags_RoundCornersTop
#else
#  define IMGUI_EX_ROUND_TOP ImDrawCornerFlags_Top
#endif

namespace imgui_ex {

    namespace {
        inline ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
            ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
            ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(
                ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
        }
        inline ImU32 MulAlpha(ImU32 c, float mul) {
            int a = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * mul);
            a = a < 0 ? 0 : (a > 255 ? 255 : a);
            return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
        }
        // 颜色微调：变亮
        inline ImU32 Lighten(ImU32 c, float amt) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x = ImMin(v.x + amt, 1.0f); v.y = ImMin(v.y + amt, 1.0f); v.z = ImMin(v.z + amt, 1.0f);
            return ImGui::ColorConvertFloat4ToU32(v);
        }
        // 颜色微调：变暗
        inline ImU32 Darken(ImU32 c, float amt) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.x = ImMax(v.x - amt, 0.0f); v.y = ImMax(v.y - amt, 0.0f); v.z = ImMax(v.z - amt, 0.0f);
            return ImGui::ColorConvertFloat4ToU32(v);
        }
    }

    bool BeginWithTitleBar(const char* name,
        bool* p_open, ImGuiWindowFlags flags, const TitleBarStyle& tb)
    {
        ImGuiWindowFlags finalFlags = flags | ImGuiWindowFlags_NoTitleBar;
        bool visible = ImGui::Begin(name, p_open, finalFlags);
        if (!visible) return false;

        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;
        const ImGuiStyle& style = ImGui::GetStyle();

        const float border = style.WindowBorderSize;
        const float titleH = tb.height;
        const float rounding = window->WindowRounding;
        const float closeAreaW = tb.showClose ? (tb.closeButtonSize + tb.titlePaddingX + 8.0f) : 0.0f;

        const ImVec2 winPos = window->Pos;
        const ImVec2 winSize = window->Size;
        const ImVec2 tMin(winPos.x + border, winPos.y + border);
        const ImVec2 tMax(winPos.x + winSize.x - border, winPos.y + border + titleH);

        const ImRect titleBarRect(tMin, tMax);
        const bool isHoveredTitle = (g.HoveredWindow == window) && titleBarRect.Contains(g.IO.MousePos);

        // --- 动画状态 ---
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const float k = ImClamp(g.IO.DeltaTime * tb.hoverSpeed, 0.0f, 1.0f);

        // 激活状态渐变
        const ImGuiID idActive = window->GetID("##tbar_active");
        float activeT = storage->GetFloat(idActive, window->Active ? 1.0f : 0.0f);
        activeT = ImLerp(activeT, window->Active ? 1.0f : 0.0f, k);
        storage->SetFloat(idActive, activeT);

        // Hover 状态渐变
        const ImGuiID idHover = window->GetID("##tbar_hover");
        float hoverT = storage->GetFloat(idHover, 0.0f);
        hoverT = ImLerp(hoverT, isHoveredTitle ? 1.0f : 0.0f, k);
        storage->SetFloat(idHover, hoverT);

        // --- 1. 背景（纯色圆角 + 悬浮提亮）---
        ImU32 bgCol = LerpColor(tb.bgInactive, tb.bgActive, activeT);
        // 鼠标悬浮时整体提亮一点点
        bgCol = LerpColor(bgCol, Lighten(bgCol, 0.05f), hoverT);

        // 使用支持圆角的 AddRectFilled
        dl->AddRectFilled(tMin, tMax, bgCol, rounding, IMGUI_EX_ROUND_TOP);

        // --- 2. 顶部高光 & 底部内阴影 ---
        // 高光（顶部 1px 白线）—— 让标题栏有凸起的立体感
        dl->AddLine(tMin, ImVec2(tMax.x, tMin.y), IM_COL32(255, 255, 255, 25), 1.0f);
        // 阴影（底部 1px 黑线）—— 分隔标题栏与下方内容
        dl->AddLine(ImVec2(tMin.x, tMax.y - 0.5f), ImVec2(tMax.x, tMax.y - 0.5f), IM_COL32(0, 0, 0, 80), 1.0f);

        // --- 3. 左侧 App 图标（模拟）---
        const float iconSize = titleH * 0.35f;
        const ImVec2 iconCenter(tMin.x + tb.titlePaddingX + iconSize * 0.5f, (tMin.y + tMax.y) * 0.5f);
        dl->AddCircleFilled(iconCenter, iconSize * 0.5f, IM_COL32(100, 180, 255, 255), 16); // 画个蓝色小圆点做点缀

        // --- 4. 标题文字（带阴影）---
        const ImU32 txtCol = LerpColor(tb.textInactive, tb.textActive, activeT);
        const ImVec2 titleTextSize = ImGui::CalcTextSize(name, nullptr, true);
        // 文字起始X避开左侧图标
        const ImVec2 titleTextPos(
            tMin.x + tb.titlePaddingX + iconSize + 8.0f,
            tMin.y + (titleH - titleTextSize.y) * 0.5f);
        // 文字阴影（提升可读性）
        dl->AddText(ImVec2(titleTextPos.x + 1, titleTextPos.y + 1), IM_COL32(0, 0, 0, 100), name);
        dl->AddText(titleTextPos, txtCol, name);

        // --- 5. 关闭按钮（常驻半透明 + 悬浮变红）---
        bool closeHovered = false;
        if (tb.showClose) {
            const float closeBtnHalf = tb.closeButtonSize * 0.5f;
            const ImVec2 closeCenter(
                tMax.x - tb.titlePaddingX - closeBtnHalf,
                (tMin.y + tMax.y) * 0.5f);
            const ImRect closeRect(
                ImVec2(closeCenter.x - closeBtnHalf, closeCenter.y - closeBtnHalf),
                ImVec2(closeCenter.x + closeBtnHalf, closeCenter.y + closeBtnHalf));

            closeHovered = (g.HoveredWindow == window) && closeRect.Contains(g.IO.MousePos);

            const ImGuiID idClose = window->GetID("##tbar_close");
            float closeHoverT = storage->GetFloat(idClose, 0.0f);
            closeHoverT = ImLerp(closeHoverT, closeHovered ? 1.0f : 0.0f, k);
            storage->SetFloat(idClose, closeHoverT);

            // 常驻：画一个极淡的底圆，hover 时变红变大
            const float baseAlpha = 0.15f + closeHoverT * 0.85f;
            const ImU32 closeBgCol = LerpColor(IM_COL32(255, 255, 255, 40), tb.closeHoverBg, closeHoverT);
            dl->AddCircleFilled(closeCenter, closeBtnHalf, MulAlpha(closeBgCol, baseAlpha), 24);

            // X 图标
            const ImU32 xCol = LerpColor(txtCol, IM_COL32(255, 255, 255, 255), closeHoverT);
            const float r = tb.closeIconSize * 0.5f;
            dl->AddLine(ImVec2(closeCenter.x - r, closeCenter.y - r), ImVec2(closeCenter.x + r, closeCenter.y + r), xCol, tb.closeIconThick);
            dl->AddLine(ImVec2(closeCenter.x - r, closeCenter.y + r), ImVec2(closeCenter.x + r, closeCenter.y - r), xCol, tb.closeIconThick);

            if (closeHovered && ImGui::IsMouseClicked(0)) {
                if (p_open) *p_open = false; // 如果用户没传p_open，这里无法关闭，但UI依然显示
            }
        }

        // --- 6. 拖动窗口 ---
        const bool dragHovered = (g.HoveredWindow == window) && (g.IO.MousePos.y < tMax.y);
        const bool canMove = !(window->Flags & ImGuiWindowFlags_NoMove);
        if (dragHovered && !closeHovered && canMove && ImGui::IsMouseClicked(0))
            ImGui::StartMouseMovingWindow(window);

        // --- 7. 下推内容 ---
        ImGui::SetCursorPosY(border + titleH + tb.contentGapY);

        return true;
    }

} // namespace imgui_ex
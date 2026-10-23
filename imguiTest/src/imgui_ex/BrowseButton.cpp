#include <imgui_ex/BrowseButton.hpp>
namespace imgui_ex {

    // ImGui 1.83+ 用 ImDrawFlags_RoundCornersAll；更老版本用 ImDrawCornerFlags_All
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18300
#  define BB_ROUND_ALL ImDrawFlags_RoundCornersAll
#else
#  define BB_ROUND_ALL ImDrawCornerFlags_All
#endif


    namespace detail {
        inline ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
            ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
            ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(
                ca.x + (cb.x - ca.x) * t,
                ca.y + (cb.y - ca.y) * t,
                ca.z + (cb.z - ca.z) * t,
                ca.w + (cb.w - ca.w) * t));
        }

        inline ImU32 MulAlpha(ImU32 c, float mul)
        {
            int a = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * mul);
            a = a < 0 ? 0 : (a > 255 ? 255 : a);
            return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
        }
    }

    bool BrowserButton(const char* label, const ImVec2& size, bool enabled,
        const BrowserButtonStyle& style)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& istyle = ImGui::GetStyle();
        const ImGuiID id = window->GetID(label);

        // ---------- 1. 计算尺寸 ----------
        const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
        ImVec2 btnSize = size;
        if (btnSize.x <= 0.0f) btnSize.x = textSize.x + style.padding.x * 2.0f;
        if (btnSize.y <= 0.0f) btnSize.y = textSize.y + style.padding.y * 2.0f;
        btnSize.y = ImMax(btnSize.y, ImGui::GetFrameHeight());   // 修复点

        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + btnSize.x, pos.y + btnSize.y));

        ImGui::ItemSize(bb, istyle.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        // ---------- 2. 交互 ----------
        bool hovered = false, held = false;
        bool pressed = false;
        if (enabled)
            pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

        const bool isActive = enabled && held && hovered;   // 拖出后恢复，贴合浏览器行为

        // ---------- 3. hover 过渡动画 ----------
        ImGuiStorage* storage = ImGui::GetStateStorage();
        float t = storage->GetFloat(id, 0.0f);
        {
            const float target = (hovered && enabled) ? 1.0f : 0.0f;
            const float k = ImClamp(g.IO.DeltaTime * style.hoverSpeed, 0.0f, 1.0f);
            t = ImLerp(t, target, k);
            if (ImFabs(t - target) < 0.002f) t = target;
            storage->SetFloat(id, t);
        }

        ImDrawList* dl = window->DrawList;
        const float r = style.rounding;
        const float alphaMul = enabled ? 1.0f : 0.45f;

        // ---------- 4. 按下时整体下沉 1px ----------
        const ImVec2 off = isActive ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 bmin(bb.Min.x + off.x, bb.Min.y + off.y);
        const ImVec2 bmax(bb.Max.x + off.x, bb.Max.y + off.y);

        // ---------- 5. 柔和阴影：多层半透明圆角矩形近似 ----------
        if (style.shadow && enabled)
        {
            const int   layers = 5;
            const ImU32 rgb = style.shadowColor & ~IM_COL32_A_MASK;
            const float baseA = ((style.shadowColor >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            for (int i = layers; i >= 1; --i)
            {
                const float f = (float)i / (float)layers;   // 1 -> 0
                const float expand = f * 3.5f;
                const float a = baseA * (1.0f - f * 0.75f); // 外层更淡
                const ImU32 col = rgb | ((ImU32)(a * 255.0f) << IM_COL32_A_SHIFT);
                dl->AddRectFilled(
                    ImVec2(bmin.x - expand, bmin.y - expand + 1.0f),
                    ImVec2(bmax.x + expand, bmax.y + expand + 2.0f),
                    col, r + expand);
            }
        }


        // ---------- 6. 背景 ----------
        ImU32 bg = isActive ? style.bgActive
            : detail::LerpColor(style.bgNormal, style.bgHovered, t);
        bg = detail::MulAlpha(bg, alphaMul);
        dl->AddRectFilled(bmin, bmax, bg, r, BB_ROUND_ALL);

        // ---------- 7. 边框 ----------
        if (style.borderWidth > 0.0f)
            dl->AddRect(bmin, bmax, detail::MulAlpha(style.borderColor, alphaMul),
                r, BB_ROUND_ALL, style.borderWidth);

        // ---------- 8. 文字居中 ----------
        const ImU32  textCol = detail::MulAlpha(style.textColor, alphaMul);
        const ImVec2 textPos(bmin.x + (bmax.x - bmin.x - textSize.x) * 0.5f,
            bmin.y + (bmax.y - bmin.y - textSize.y) * 0.5f);
        dl->AddText(textPos, textCol, label);

        // ---------- 9. 键盘焦点外圈 ----------
        if (enabled && ImGui::IsItemFocused())
            dl->AddRect(ImVec2(bmin.x - 2.0f, bmin.y - 2.0f),
                ImVec2(bmax.x + 2.0f, bmax.y + 2.0f),
                style.focusColor, r + 2.0f, BB_ROUND_ALL, 2.0f);

        return pressed;
    }

}
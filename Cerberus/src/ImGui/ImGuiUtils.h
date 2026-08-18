#pragma once
#include "ImGui/ImGuiColors.h"
#include <imgui/imgui_internal.h>
#include "Renderer/Texture.h"

namespace cerberus::ui
{
    class ScopedStyle
    {
    public:
        ScopedStyle(const ScopedStyle&) = delete;
        ScopedStyle operator=(const ScopedStyle&) = delete;
        template<typename T>
        ScopedStyle(ImGuiStyleVar styleVar, T value) { ImGui::PushStyleVar(styleVar, value); }
        ~ScopedStyle() { ImGui::PopStyleVar(); }
    };

    class ScopedColor
    {
    public:
        ScopedColor(const ScopedColor&) = delete;
        ScopedColor operator=(const ScopedColor&) = delete;
        template<typename T>
        ScopedColor(ImGuiCol colourId, T colour) { ImGui::PushStyleColor(colourId, colour); }
        ~ScopedColor() { ImGui::PopStyleColor(); }
    };

    class ScopedID
    {
    public:
        ScopedID(const ScopedID&) = delete;
        ScopedID operator=(const ScopedID&) = delete;
        template<typename T>
        ScopedID(T id) { ImGui::PushID(id); }
        ~ScopedID() { ImGui::PopID(); }
    };

    class ScopedFont
    {
    public:
        ScopedFont(const ScopedFont&) = delete;
        ScopedFont operator=(const ScopedFont&) = delete;
        ScopedFont(ImFont* font) { ImGui::PushFont(font); }
        ~ScopedFont() { ImGui::PopFont(); }
    };

    class ScopedColourStack
    {
    public:
        ScopedColourStack(const ScopedColourStack&) = delete;
        ScopedColourStack operator=(const ScopedColourStack&) = delete;

        template <typename ColourType, typename... OtherColours>
        ScopedColourStack(ImGuiCol firstColourID, ColourType firstColour, OtherColours&& ... otherColourPairs)
            : m_Count((sizeof... (otherColourPairs) / 2) + 1)
        {
            static_assert ((sizeof... (otherColourPairs) & 1u) == 0,
                "ScopedColourStack constructor expects a list of pairs of colour IDs and colours as its arguments");

            PushColour(firstColourID, firstColour, std::forward<OtherColours>(otherColourPairs)...);
        }

        ~ScopedColourStack() { ImGui::PopStyleColor(m_Count); }

    private:
        int m_Count;

        template <typename ColourType, typename... OtherColours>
        void PushColour(ImGuiCol colourID, ColourType colour, OtherColours&& ... otherColourPairs)
        {
            if constexpr (sizeof... (otherColourPairs) == 0)
            {
                ImGui::PushStyleColor(colourID, colour);
            }
            else
            {
                ImGui::PushStyleColor(colourID, colour);
                PushColour(std::forward<OtherColours>(otherColourPairs)...);
            }
        }
    };

    //=========================================================================================

    static void ShiftCursorX(float distance)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + distance);
    }

    static void ShiftCursorY(float distance)
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + distance);
    }

    static void ShiftCursor(float x, float y)
    {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + x, cursor.y + y));
    }

    //=========================================================================================
    /// Rectangle
    static inline ImRect GetItemRect()
    {
        return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    static inline ImRect RectExpanded(const ImRect& rect, float x, float y)
    {
        ImRect result = rect;
        result.Min.x -= x;
        result.Min.y -= y;
        result.Max.x += x;
        result.Max.y += y;
        return result;
    }

    static inline ImRect RectOffset(const ImRect& rect, float x, float y)
    {
        ImRect result = rect;
        result.Min.x += x;
        result.Min.y += y;
        result.Max.x += x;
        result.Max.y += y;
        return result;
    }

    static inline ImRect RectOffset(const ImRect& rect, ImVec2 xy)
    {
        return RectOffset(rect, xy.x, xy.y);
    }

    static ImU32 ColourWithMultipliedValue(const ImColor& color, float multiplier)
    {
        const ImVec4& colRow = color.Value;
        float hue, sat, val;
        ImGui::ColorConvertRGBtoHSV(colRow.x, colRow.y, colRow.z, hue, sat, val);
        return ImColor::HSV(hue, sat, std::min(val * multiplier, 1.0f));
    }

    ImTextureID GetTextureID(Handle<ImageView> imageView);
    ImTextureID GetTextureID(Handle<Texture2D> texture);
    ImTextureID GetTextureID(const Ref<Texture2D>& texture);

    namespace draw
    {
        static void DrawButtonImage(Handle<Texture2D> imageNormal, Handle<Texture2D> imageHovered, Handle<Texture2D> imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImVec2 rectMin, ImVec2 rectMax)
        {
            const bool isActive = ImGui::IsItemActive();
            const bool isHovered = ImGui::IsItemHovered();
            const Handle<Texture2D> image = isActive ? imagePressed : (isHovered ? imageHovered : imageNormal);
            const ImU32 tint = isActive ? tintPressed : (isHovered ? tintHovered : tintNormal);
            const ImTextureID textureID = GetTextureID(image);
            if (!textureID)
                return;

            ImGui::GetWindowDrawList()->AddImage(textureID, rectMin, rectMax, ImVec2(0, 0), ImVec2(1, 1), tint);
        };

        static void DrawButtonImage(Handle<Texture2D> imageNormal, Handle<Texture2D> imageHovered, Handle<Texture2D> imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImRect rectangle)
        {
            DrawButtonImage(imageNormal, imageHovered, imagePressed, tintNormal, tintHovered, tintPressed, rectangle.Min, rectangle.Max);
        };

        static void DrawButtonImage(Handle<Texture2D> image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImVec2 rectMin, ImVec2 rectMax)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, rectMin, rectMax);
        };

        static void DrawButtonImage(Handle<Texture2D> image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImRect rectangle)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, rectangle.Min, rectangle.Max);
        };


        static void DrawButtonImage(Handle<Texture2D> imageNormal, Handle<Texture2D> imageHovered, Handle<Texture2D> imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed)
        {
            DrawButtonImage(imageNormal, imageHovered, imagePressed, tintNormal, tintHovered, tintPressed, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        };

        static void DrawButtonImage(Handle<Texture2D> image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        };

        // Compatibility path for editor icons that are still owned through Ref.
        // New renderer resources use Handle<Texture2D>, but both forms resolve to
        // the same ResourceManager-owned ImageView representation.
        static void DrawButtonImage(const Ref<Texture2D>& imageNormal, const Ref<Texture2D>& imageHovered, const Ref<Texture2D>& imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImVec2 rectMin, ImVec2 rectMax)
        {
            const bool isActive = ImGui::IsItemActive();
            const bool isHovered = ImGui::IsItemHovered();
            const Ref<Texture2D>& image = isActive ? imagePressed : (isHovered ? imageHovered : imageNormal);
            const ImU32 tint = isActive ? tintPressed : (isHovered ? tintHovered : tintNormal);
            const ImTextureID textureID = GetTextureID(image);
            if (!textureID)
                return;

            ImGui::GetWindowDrawList()->AddImage(textureID, rectMin, rectMax, ImVec2(0, 0), ImVec2(1, 1), tint);
        };

        static void DrawButtonImage(const Ref<Texture2D>& imageNormal, const Ref<Texture2D>& imageHovered, const Ref<Texture2D>& imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImRect rectangle)
        {
            DrawButtonImage(imageNormal, imageHovered, imagePressed, tintNormal, tintHovered, tintPressed, rectangle.Min, rectangle.Max);
        };

        static void DrawButtonImage(const Ref<Texture2D>& image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImVec2 rectMin, ImVec2 rectMax)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, rectMin, rectMax);
        };

        static void DrawButtonImage(const Ref<Texture2D>& image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed,
            ImRect rectangle)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, rectangle.Min, rectangle.Max);
        };

        static void DrawButtonImage(const Ref<Texture2D>& imageNormal, const Ref<Texture2D>& imageHovered, const Ref<Texture2D>& imagePressed,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed)
        {
            DrawButtonImage(imageNormal, imageHovered, imagePressed, tintNormal, tintHovered, tintPressed, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        };

        static void DrawButtonImage(const Ref<Texture2D>& image,
            ImU32 tintNormal, ImU32 tintHovered, ImU32 tintPressed)
        {
            DrawButtonImage(image, image, image, tintNormal, tintHovered, tintPressed, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        };

        static void DrawItemActivityOutline(float rounding = 0.0f, bool drawWhenInactive = false, ImColor colourWhenActive = ImColor(80, 80, 80))
        {
            auto* drawList = ImGui::GetWindowDrawList();
            const ImRect rect = RectExpanded(GetItemRect(), 1.0f, 1.0f);
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            {
                drawList->AddRect(rect.Min, rect.Max,
                    ImColor(60, 60, 60), rounding, 0, 1.5f);
            }
            if (ImGui::IsItemActive())
            {
                drawList->AddRect(rect.Min, rect.Max,
                    colourWhenActive, rounding, 0, 1.0f);
            }
            else if (!ImGui::IsItemHovered() && drawWhenInactive)
            {
                drawList->AddRect(rect.Min, rect.Max,
                    ImColor(50, 50, 50), rounding, 0, 1.0f);
            }
        };

        //=========================================================================================
        /// Lines
        static void Underline(bool fullWidth = false, float offsetX = 0.0f, float offsetY = -1.0f)
        {
            if (fullWidth)
            {
                if (ImGui::GetCurrentWindow()->DC.CurrentColumns != nullptr)
                    ImGui::PushColumnsBackground();
                else if (ImGui::GetCurrentTable() != nullptr)
                    ImGui::TablePushBackgroundChannel();
            }

            const float width = fullWidth ? ImGui::GetWindowWidth() : ImGui::GetContentRegionAvail().x;
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x + offsetX, cursor.y + offsetY),
                ImVec2(cursor.x + width, cursor.y + offsetY),
                colors::theme::backgroundDark, 1.0f);

            if (fullWidth)
            {
                if (ImGui::GetCurrentWindow()->DC.CurrentColumns != nullptr)
                    ImGui::PopColumnsBackground();
                else if (ImGui::GetCurrentTable() != nullptr)
                    ImGui::TablePopBackgroundChannel();
            }
        }
    }
}

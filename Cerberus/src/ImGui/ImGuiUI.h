#pragma once
#include "Renderer/Texture.h"
#include "ImGui/ImGuiUtils.h"
#include <glm/gtc/type_ptr.hpp>
#include "Asset/AssetManager.h"
namespace cerberus::ui
{
	static int s_UIContextID = 0;
	static uint32_t s_Counter = 0;
	static char s_IDBuffer[16];
	static char* s_MultilineBuffer = nullptr;

	static const char* GenerateID()
	{
		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		return &s_IDBuffer[0];
	}

	static bool IsMouseEnabled()
	{
		return ImGui::GetIO().ConfigFlags & ~ImGuiConfigFlags_NoMouse;
	}

	static void SetMouseEnabled(const bool enable)
	{
		if (enable)
			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		else
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
	}

	static void PushID()
	{
		ImGui::PushID(s_UIContextID++);
		s_Counter = 0;
	}

	static void PopID()
	{
		ImGui::PopID();
		s_UIContextID--;
	}

	static void Separator()
	{
		ImGui::Separator();
	}

	// note: prefer ScopedItemFlags(ImGuiItemFlags_Disabled) over PushItemDisabled/PopItemDisabled
	static void PushItemDisabled()
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
	}

	static bool IsItemDisabled()
	{
		return ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
	}

	static void PopItemDisabled()
	{
		ImGui::PopItemFlag();
	}

	static bool PropertyGridHeader(const std::string& name, bool openByDefault = false)
	{
		ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_Framed
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_AllowItemOverlap
			| ImGuiTreeNodeFlags_FramePadding;

		if (openByDefault)
			treeNodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

		bool open = false;
		const float framePaddingX = 6.0f;
		const float framePaddingY = 6.0f; // affects height of the header

		ui::ScopedStyle headerRounding(ImGuiStyleVar_FrameRounding, 0.0f);
		ui::ScopedStyle headerPaddingAndHeight(ImGuiStyleVar_FramePadding, ImVec2{ framePaddingX, framePaddingY });

		ImGui::PushID(name.c_str());
		open = ImGui::TreeNodeEx("##dummy_id", treeNodeFlags, name.c_str());
		ImGui::PopID();

		return open;
	}

	static bool BeginTreeNode(const char* name, bool defaultOpen = true)
	{
		ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if (defaultOpen)
			treeNodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

		return ImGui::TreeNodeEx(name, treeNodeFlags);
	}

	static void EndTreeNode()
	{
		ImGui::TreePop();
	}

	// MenuBar which allows you to specify its rectangle
	static bool BeginMenuBar(const ImRect& barRectangle)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;
		/*if (!(window->Flags & ImGuiWindowFlags_MenuBar))
			return false;*/

		IM_ASSERT(!window->DC.MenuBarAppending);
		ImGui::BeginGroup(); // Backup position on layer 0 // FIXME: Misleading to use a group for that backup/restore
		ImGui::PushID("##menubar");

		const ImVec2 padding = window->WindowPadding;

		// We don't clip with current window clipping rectangle as it is already set to the area below. However we clip with window full rect.
		// We remove 1 worth of rounding to Max.x to that text in long menus and small windows don't tend to display over the lower-right rounded area, which looks particularly glitchy.
		ImRect bar_rect = ui::RectOffset(barRectangle, 0.0f, padding.y);// window->MenuBarRect();
		ImRect clip_rect(IM_ROUND(ImMax(window->Pos.x, bar_rect.Min.x + window->WindowBorderSize + window->Pos.x - 10.0f)), IM_ROUND(bar_rect.Min.y + window->WindowBorderSize + window->Pos.y),
			IM_ROUND(ImMax(bar_rect.Min.x + window->Pos.x, bar_rect.Max.x - ImMax(window->WindowRounding, window->WindowBorderSize))), IM_ROUND(bar_rect.Max.y + window->Pos.y));

		clip_rect.ClipWith(window->OuterRectClipped);
		ImGui::PushClipRect(clip_rect.Min, clip_rect.Max, false);

		// We overwrite CursorMaxPos because BeginGroup sets it to CursorPos (essentially the .EmitItem hack in EndMenuBar() would need something analogous here, maybe a BeginGroupEx() with flags).
		window->DC.CursorPos = window->DC.CursorMaxPos = ImVec2(bar_rect.Min.x + window->Pos.x, bar_rect.Min.y + window->Pos.y);
		window->DC.LayoutType = ImGuiLayoutType_Horizontal;
		window->DC.NavLayerCurrent = ImGuiNavLayer_Menu;
		window->DC.MenuBarAppending = true;
		ImGui::AlignTextToFramePadding();
		return true;
	}

	static void EndMenuBar()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;
		ImGuiContext& g = *GImGui;

		// Nav: When a move request within one of our child menu failed, capture the request to navigate among our siblings.
		if (ImGui::NavMoveRequestButNoResultYet() && (g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right) && (g.NavWindow->Flags & ImGuiWindowFlags_ChildMenu))
		{
			ImGuiWindow* nav_earliest_child = g.NavWindow;
			while (nav_earliest_child->ParentWindow && (nav_earliest_child->ParentWindow->Flags & ImGuiWindowFlags_ChildMenu))
				nav_earliest_child = nav_earliest_child->ParentWindow;
			if (nav_earliest_child->ParentWindow == window && nav_earliest_child->DC.ParentLayoutType == ImGuiLayoutType_Horizontal && g.NavMoveRequestForward == ImGuiNavForward_None)
			{
				// To do so we claim focus back, restore NavId and then process the movement request for yet another frame.
				// This involve a one-frame delay which isn't very problematic in this situation. We could remove it by scoring in advance for multiple window (probably not worth the hassle/cost)
				const ImGuiNavLayer layer = ImGuiNavLayer_Menu;
				IM_ASSERT(window->DC.NavLayersActiveMaskNext & (1 << layer)); // Sanity check
				ImGui::FocusWindow(window);
				ImGui::SetNavID(window->NavLastIds[layer], layer, 0, window->NavRectRel[layer]);
				g.NavDisableHighlight = true; // Hide highlight for the current frame so we don't see the intermediary selection.
				g.NavDisableMouseHover = g.NavMousePosDirty = true;
				g.NavMoveRequestForward = ImGuiNavForward_ForwardQueued;
				ImGui::NavMoveRequestCancel();
			}
		}

		IM_MSVC_WARNING_SUPPRESS(6011); // Static Analysis false positive "warning C6011: Dereferencing NULL pointer 'window'"
		//IM_ASSERT(window->Flags & ImGuiWindowFlags_MenuBar);
		IM_ASSERT(window->DC.MenuBarAppending);
		ImGui::PopClipRect();
		ImGui::PopID();
		window->DC.MenuBarOffset.x = window->DC.CursorPos.x - window->Pos.x; // Save horizontal position so next append can reuse it. This is kinda equivalent to a per-layer CursorPos.
		g.GroupStack.back().EmitItem = false;
		ImGui::EndGroup(); // Restore position on layer 0
		window->DC.LayoutType = ImGuiLayoutType_Vertical;
		window->DC.NavLayerCurrent = ImGuiNavLayer_Main;
		window->DC.MenuBarAppending = false;
	}

	static void BeginPropertyGrid()
	{
		PushID();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
		ImGui::Columns(2);
	}

	static void EndPropertyGrid()
	{
		ImGui::Columns(1);
		ui::draw::Underline();
		ImGui::PopStyleVar(2); // ItemSpacing, FramePadding
		ui::ShiftCursorY(18.0f);
		PopID();
	}

	static bool PropertySlider(const char* label, int& value, int min, int max)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::SliderInt(s_IDBuffer, &value, min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, float& value, float min, float max)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::SliderFloat(s_IDBuffer, &value, min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec2& value, float min, float max)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::SliderFloat2(s_IDBuffer, glm::value_ptr(value), min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec3& value, float min, float max)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::SliderFloat3(s_IDBuffer, glm::value_ptr(value), min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertySlider(const char* label, glm::vec4& value, float min, float max)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::SliderFloat4(s_IDBuffer, glm::value_ptr(value), min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool Property(const char* label, bool& value)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::Checkbox(s_IDBuffer, &value))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}


	static bool Property(const char* label, float& value, float delta = 0.1f, float min = 0.0f, float max = 0.0f)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::DragFloat(s_IDBuffer, &value, delta, min, max))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec2& value, float delta = 0.1f)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::DragFloat2(s_IDBuffer, glm::value_ptr(value), delta))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertyColor(const char* label, glm::vec3& value)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::ColorEdit3(s_IDBuffer, glm::value_ptr(value)))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool PropertyColor(const char* label, glm::vec4& value)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::ColorEdit4(s_IDBuffer, glm::value_ptr(value)))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec3& value, float delta = 0.1f)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::DragFloat3(s_IDBuffer, glm::value_ptr(value), delta))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	static bool Property(const char* label, glm::vec4& value, float delta = 0.1f)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		s_IDBuffer[0] = '#';
		s_IDBuffer[1] = '#';
		memset(s_IDBuffer + 2, 0, 14);
		sprintf_s(s_IDBuffer + 2, 14, "%o", s_Counter++);

		if (IsItemDisabled())
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

		if (ImGui::DragFloat4(s_IDBuffer, glm::value_ptr(value), delta))
			modified = true;

		if (IsItemDisabled())
			ImGui::PopStyleVar();

		if (!IsItemDisabled())
			draw::DrawItemActivityOutline(2.0f, true, colors::theme::accent);

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();

		return modified;
	}

	template<typename TAssetType>
	static bool PropertyAssetReferenceTarget(const char* label, AssetHandle& outHandle)
	{
		const ImU32 buttonCol = IM_COL32(56, 56, 56, 200);
		const ImU32 buttonHov = IM_COL32(70, 70, 70, 255);
		const ImU32 buttonAct = IM_COL32(56, 56, 56, 255);

		ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
			ImGuiCol_ButtonHovered, buttonHov,
			ImGuiCol_ButtonActive, buttonAct);

		bool succeeded = false;
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
		float width = ImGui::GetContentRegionAvail().x;
		ui::PushID();
		{
			float itemHeight = 28.0f;
			std::string buttonText = "Null";
			// Verify if the asset handle is valid
			if (AssetManager::IsAssetHandleValid(outHandle))
			{
				auto object = AssetManager::GetAsset<TAssetType>(outHandle);
				if (object && !object->IsFlagSet(AssetFlag::Missing))
					buttonText = AssetManager::GetMetadata(outHandle).FilePath.stem().string();
				else
					buttonText = "Missing";
			}
			if (ImGui::Button(fmt::format("{}##{}", buttonText, s_Counter++).c_str(), { width, itemHeight }))
				ImGui::OpenPopup("AssetSearchPopup");

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;
			
		}
		ui::PopID();
		// Drag
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
			{
				AssetHandle assetHandle = *(AssetHandle*)data->Data;
				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset && asset->GetAssetType() == TAssetType::GetStaticType())
				{
					outHandle = assetHandle;
					succeeded = true;
				}
			}
		}
		// Verify if the asset acquired with success
		succeeded = succeeded ? true : false;

		ImGui::PopItemWidth();
		ImGui::NextColumn();
		draw::Underline();
		return succeeded;
	}
	struct PropertyAssetReferenceSettings
	{
		bool AdvanceToNextColumn = true;
		bool NoItemSpacing = false; // After label
		float WidthOffset = 0.0f;
	};

	template<typename TAssetType, typename Fn>
	static bool PropertyAssetReferenceTarget(const char* label, AssetHandle& outHandle, Fn&& targetFunc, const PropertyAssetReferenceSettings& settings = {})
	{
		const ImU32 buttonCol = IM_COL32(56, 56, 56, 200);
		const ImU32 buttonHov = IM_COL32(70, 70, 70, 255);
		const ImU32 buttonAct = IM_COL32(56, 56, 56, 255);

		ui::ScopedColourStack buttonColours(ImGuiCol_Button, buttonCol,
			ImGuiCol_ButtonHovered, buttonHov,
			ImGuiCol_ButtonActive, buttonAct);

		bool succeeded = false;
		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);
		if (settings.NoItemSpacing)
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
		float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
		ui::PushID();
		{
			float itemHeight = 28.0f;
			std::string buttonText = "Null";
			// Verify if the asset handle is valid
			if (AssetManager::IsAssetHandleValid(outHandle))
			{
				auto object = AssetManager::GetAsset<TAssetType>(outHandle);
				if (object && !object->IsFlagSet(AssetFlag::Missing))
					buttonText = AssetManager::GetMetadata(outHandle).FilePath.stem().string();
				else
					buttonText = "Missing";
			}
			if (ImGui::Button(fmt::format("{}##{}", buttonText, s_Counter++).c_str(), { width, itemHeight }))
				ImGui::OpenPopup("AssetSearchPopup");

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		}
		ui::PopID();
		// Drag
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
			{
				AssetHandle assetHandle = *(AssetHandle*)data->Data;
				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset && asset->GetAssetType() == TAssetType::GetStaticType())
				{
					outHandle = assetHandle;
					targetFunc(std::dynamic_pointer_cast<TAssetType>(asset));
					succeeded = true;
				}
			}
		}

		ImGui::PopItemWidth();
		if (settings.AdvanceToNextColumn)
			ImGui::NextColumn();
		if (settings.NoItemSpacing)
			ImGui::PopStyleVar();
		return succeeded;
	}
	///////////////////////////////////////////
	// Image
	///////////////////////////////////////////
	void ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& background_col = ImVec4(0, 0, 0, 0));
	void Image(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	void Image(const Ref<Image2D>& image, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
	void Image(const Ref<Image2D>& image, uint32_t layer, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));

}
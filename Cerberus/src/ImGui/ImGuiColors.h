#pragma once
#include <imgui/imgui.h>
namespace cerberus::colors
{
	// To experiment with editor theme live you can change these constexpr into static
	// members of a static "Theme" class and add a quick ImGui window to adjust the colour values
	namespace theme
	{
		/**/
		constexpr auto accent = IM_COL32(255, 119, 0, 255);
		constexpr auto highlight = IM_COL32(255, 119, 0, 255);
		constexpr auto highlightHovered = IM_COL32(255, 119, 0, 150);

		constexpr auto compliment = IM_COL32(78, 151, 166, 255);
		constexpr auto background = IM_COL32(28, 28, 36, 255);
		constexpr auto backgroundDark = IM_COL32(18, 18, 26, 255);
		constexpr auto titlebar = IM_COL32(32, 32, 32, 255);
		constexpr auto titlebarHovered = IM_COL32(255, 119, 0, 50);
		constexpr auto titlebarActive = IM_COL32(255, 119, 0, 100);
		//constexpr auto titlebar = IM_COL32(255, 99, 71, 255);
		constexpr auto propertyField = IM_COL32(7, 7, 15, 255);
		constexpr auto text = IM_COL32(192, 192, 192, 255);
		constexpr auto textDarker = IM_COL32(128, 128, 128, 255);
		constexpr auto groupHeader = IM_COL32(41, 41, 47, 255);
		constexpr auto backgroundPopup = IM_COL32(63, 70, 77, 255); // most blue

		
		constexpr auto Buttom = IM_COL32(92, 92, 255, 120);
		constexpr auto ButtomHovered = IM_COL32(122, 122, 255, 150);
		constexpr auto ButtomActive = IM_COL32(92, 92, 255, 150);
		/** /
		constexpr auto Buttom = IM_COL32(56, 56, 56, 200);
		constexpr auto ButtomHovered = IM_COL32(70, 70, 70, 255);
		constexpr auto ButtomActive = IM_COL32(56, 56, 56, 255);

		/**/

	}
	
}
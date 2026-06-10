include "./vendor/premake/premake_customization/solution_items.lua"

workspace "Cerberus"
	architecture "x86_64"
	startproject "Cerberus-Editor"

	configurations
	{
		"Debug",
		"Release",
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

VULKAN_SDK = os.getenv("VULKAN_SDK")

-- Include directories relative to root folder (solution directory)
IncludeDir = {}
IncludeDir["Cerberus"] = "%{wks.location}/Cerberus"
IncludeDir["GLFW"] = "%{wks.location}/Cerberus/vendor/GLFW/include"
IncludeDir["ImGui"] = "%{wks.location}/Cerberus/vendor/ImGui"
IncludeDir["yaml_cpp"] = "%{wks.location}/Cerberus/vendor/yaml-cpp/include"
IncludeDir["glm"] = "%{wks.location}/Cerberus/vendor/glm"
IncludeDir["spdlog"] = "%{wks.location}/Cerberus/vendor/spdlog/include"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"
IncludeDir["assimp"] = "%{wks.location}/Cerberus/vendor/assimp/include"
IncludeDir["entt"] = "%{wks.location}/Cerberus/vendor/entt/include"
IncludeDir["stb"] = "%{wks.location}/Cerberus/vendor/stb/include"
IncludeDir["shaderc_util"] = "%{wks.location}/Cerberus/vendor/shaderc/libshaderc_util/include"
IncludeDir["shaderc"] = "%{wks.location}/Cerberus/vendor/shaderc/include"

LibraryDir = {}

LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"

Library = {}

Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] = "%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_sharedd.lib"
Library["ShaderC_Utils_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_utild.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsld.lib"
Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK}/SPIRV-Toolsd.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["ShaderC_Utils_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_util.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"

-- Temporarily use our own SPIRV-Cross binaries
-- Vulkan SDK 1.3.204.1 (currently latest) shipped with a critical bug for us (see https://github.com/KhronosGroup/SPIRV-Cross/issues/1879)
Library["SPIRV_Cross_Debug"] = "%{wks.location}/Cerberus/vendor/SPIRV-Cross/lib/spirv-cross-cored.lib"
Library["SPIRV_Cross_Release"] = "%{wks.location}/Cerberus/vendor/SPIRV-Cross/lib/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{wks.location}/Cerberus/vendor/SPIRV-Cross/lib/spirv-cross-glsld.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{wks.location}/Cerberus/vendor/SPIRV-Cross/lib/spirv-cross-glsl.lib"

group "Dependencies"
	include "vendor/premake"
	include "Cerberus/vendor/GLFW"
	include "Cerberus/vendor/ImGui"
	include "Cerberus/vendor/yaml-cpp"
group ""

include "Cerberus"
include "Cerberus-Editor"
include "Cerberus-Runtime"
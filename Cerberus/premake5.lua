project "Cerberus"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "pch.h"
	pchsource "src/pch.cpp"

	files
	{
		"src/**.h",
		"src/**.cpp",
		"vendor/VulkanMemoryAllocator/**.cpp",
		"vendor/VulkanMemoryAllocator/**.h",
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE"
	}

	includedirs
	{
		"src",
		"vendor",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.yaml_cpp}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.spdlog}",
		"%{IncludeDir.VulkanSDK}",
		"%{IncludeDir.assimp}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.shaderc_util}",
		"%{IncludeDir.shaderc}"
	}
		
	links
	{
		"GLFW",
		"ImGui",
		"yaml-cpp",
		"assimp",
		"%{Library.Vulkan}",
		"%{Library.VulkanUtils}",
	}

	filter "system:windows"
		systemversion "latest"

		defines
		{
			"NV_PLATFORM_WINDOWS",
			-- fmt (vendored by spdlog) still uses stdext::checked_array_iterator for
			-- bigint formatting; silence the MSVC STL deprecation warning it triggers.
			"_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING",
		}

		-- shaderc's import lib and vulkan-1.lib both emit a __NULL_IMPORT_DESCRIPTOR
		-- symbol; harmless duplicate, the linker already keeps the first one.
		linkoptions { "/ignore:4006" }

	filter "configurations:Debug"
		defines "NV_DEBUG"
		runtime "Debug"
		symbols "On"

		links
		{
			"%{Library.ShaderC_Debug}",
			"%{Library.ShaderC_Utils_Debug}",
			"%{Library.SPIRV_Cross_Debug}",
			"%{Library.SPIRV_Cross_GLSL_Debug}",
			"%{Library.SPIRV_Tools_Debug}",
		}

	filter "configurations:Release"
		defines "NV_RELEASE"
		runtime "Release"
		optimize "On"

		links
		{
			"%{Library.ShaderC_Release}",
			"%{Library.ShaderC_Utils_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_GLSL_Release}",
		}

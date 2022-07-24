project "Cerberus"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
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
	}
		
	links
	{
		"GLFW",
		"ImGui",
		"yaml-cpp",
		"%{Library.Vulkan}",
		"%{Library.VulkanUtils}",
	}

	filter "system:windows"
		systemversion "latest"

		defines
		{
			"NV_PLATFORM_WINDOWS",
		}

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
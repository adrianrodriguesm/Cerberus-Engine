project "Cerberus-Editor"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
	}

	includedirs
	{
		"src",
		"../Cerberus/src",
		"../Cerberus/vendor",
		"%{IncludeDir.glm}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.spdlog}",
		"%{IncludeDir.VulkanSDK_LocalInclude}",
		"%{IncludeDir.VulkanSDK}",
		"%{IncludeDir.entt}",
	}

	links 
	{ 
		"Cerberus",
		"GLFW",
		"%{Library.Vulkan}",
		"%{Library.VulkanUtils}",
		"assimp",
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

	filter "configurations:Release"
		defines "NV_RELEASE"
		runtime "Release"
		optimize "On"
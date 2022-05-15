project "Cerberus-Runtime"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
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
			"../Cerberus/vendor/assimp/bin/Debug/assimp-vc141-mtd.lib",
		}

		postbuildcommands 
		{
			'{COPY} "../Cerberus/vendor/assimp/bin/Debug/assimp-vc141-mtd.dll" "%{cfg.targetdir}"',	
			'{COPY} "../Cerberus/vendor/VulkanSDK/Bin/shaderc_sharedd.dll" "%{cfg.targetdir}"'
		}

	filter "configurations:Release"
		defines "NV_RELEASE"
		runtime "Release"
		optimize "On"

		links
		{
			"../Cerberus/vendor/assimp/bin/Release/assimp-vc141-mt.lib",
		}

		postbuildcommands 
		{
			'{COPY} "../Cerberus/vendor/assimp/bin/Release/assimp-vc141-mt.dll" "%{cfg.targetdir}"'
		}

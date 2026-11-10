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
			-- fmt (vendored by spdlog) still uses stdext::checked_array_iterator for
			-- bigint formatting; silence the MSVC STL deprecation warning it triggers.
			"_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING",
		}

		-- One of assimp's bundled third-party libs pulls in the static CRT; we build
		-- against the dynamic CRT, and the linker already prefers it over LIBCMT(D).
		-- The prebuilt shaderc_util/SPIRV-Cross debug libs also ship without matching
		-- PDBs, which is harmless (no debug info for those objects specifically).
		linkoptions { "/NODEFAULTLIB:LIBCMT", "/NODEFAULTLIB:LIBCMTD", "/ignore:4099" }

	filter "configurations:Debug"
		defines "NV_DEBUG"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines "NV_RELEASE"
		runtime "Release"
		optimize "On"
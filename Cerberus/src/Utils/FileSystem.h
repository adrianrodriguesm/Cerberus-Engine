#pragma once
#include <filesystem>
#include <string>

namespace cerberus
{
	struct ProjectConfig
	{
		std::string AssetDirectory = "Resources";
		std::string AssetRegistryPath = "Resources/AssetRegistry.cregistry";

		std::string MeshPath;
		std::string MeshSourcePath;
	};

	class FileSystem
	{
	public:
		static void Init(const std::filesystem::path& engineRoot, const std::filesystem::path& projectRoot);

		static const std::filesystem::path& GetEngineRoot();
		static const std::filesystem::path& GetProjectRoot();
		static std::filesystem::path GetEngineShaderDirectory();
		static std::filesystem::path GetProjectShaderDirectory();

		// Resolves explicit virtual paths such as:
		//   engine://Shaders/RayTracing/Raytrace.rgen
		//   project://Shaders/PBR.glsl
		// Legacy paths are kept working by searching the project root first and then the engine root.
		static std::filesystem::path ResolvePath(const std::string& filepath);
		static std::filesystem::path ResolveFileSystemPath(const std::string& filepath);
		static bool IsVirtualPath(const std::string& filepath);

		static std::filesystem::path GetRelativePathFromAbsolute(const std::filesystem::path absolutePath);
		static std::string GetProjectPath();
		static bool Exists(const std::filesystem::path& filepath);
		static bool Exists(const std::string& filepath);

		static bool CreateDirectory(const std::filesystem::path& directory);
		static bool CreateDirectory(const std::string& directory);

		static bool DeleteFile(const std::filesystem::path& filepath);
		static bool MoveFile(const std::filesystem::path& filepath, const std::filesystem::path& dest);
		static bool CopyFile(const std::filesystem::path& filepath, const std::filesystem::path& dest);
		static bool IsDirectory(const std::filesystem::path& filepath);

		static bool Move(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath);
		static bool Copy(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath);
		static bool Rename(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath);
		static bool RenameFilename(const std::filesystem::path& oldFilepath, const std::string& newName);


		static bool ShowFileInExplorer(const std::filesystem::path& path);
		static bool OpenDirectoryInExplorer(const std::filesystem::path& path);
		static bool OpenExternally(const std::filesystem::path& path);

		static ProjectConfig& GetProjectConfig();
	};
}
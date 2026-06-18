#include "pch.h"
#include "Utils/FileSystem.h"
#include "shellapi.h"
#include <array>
#include <algorithm>
#include <vector>

namespace cerberus
{

	namespace
	{
		std::filesystem::path s_EngineRoot;
		std::filesystem::path s_ProjectRoot;

		bool HasCerberusProjectShape(const std::filesystem::path& candidate, const char* folderName)
		{
			if (!std::filesystem::exists(candidate) || !std::filesystem::is_directory(candidate))
				return false;

			// Prefer strong validation, but keep it flexible for copied/dist folders.
			if (std::filesystem::exists(candidate / "Resources"))
				return true;
			if (std::filesystem::exists(candidate / "src"))
				return true;
			return candidate.filename().string() == folderName;
		}

		std::filesystem::path FindRootCandidate(const std::filesystem::path& requestedPath, const char* folderName)
		{
			std::filesystem::path current = std::filesystem::current_path();
			std::vector<std::filesystem::path> candidates;

			if (!requestedPath.empty())
			{
				candidates.push_back(requestedPath);
				candidates.push_back(current / requestedPath);
			}

			std::filesystem::path search = current;
			for (uint32_t i = 0; i < 8; i++)
			{
				candidates.push_back(search / folderName);
				candidates.push_back(search / ".." / folderName);

				if (!search.has_parent_path() || search.parent_path() == search)
					break;
				search = search.parent_path();
			}

			for (const std::filesystem::path& candidate : candidates)
			{
				std::error_code ec;
				std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(candidate), ec);
				if (ec)
					absolute = std::filesystem::absolute(candidate);

				if (HasCerberusProjectShape(absolute, folderName))
					return absolute;
			}

			return std::filesystem::absolute(requestedPath.empty() ? std::filesystem::path(folderName) : requestedPath);
		}

		std::string NormalizeSlashes(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		std::filesystem::path StripOptionalResourcesPrefix(std::filesystem::path relative)
		{
			std::string normalized = NormalizeSlashes(relative.string());
			const std::string resourcesPrefix = "Resources/";
			if (normalized.rfind(resourcesPrefix, 0) == 0)
				normalized = normalized.substr(resourcesPrefix.size());
			return std::filesystem::path(normalized);
		}
	}

	void FileSystem::Init(const std::filesystem::path& engineRoot, const std::filesystem::path& projectRoot)
	{
		s_EngineRoot = FindRootCandidate(engineRoot, "Cerberus");
		s_ProjectRoot = FindRootCandidate(projectRoot, "Cerberus-Editor");

		NV_CORE_INFO("Engine root: {0}", s_EngineRoot.string());
		NV_CORE_INFO("Project root: {0}", s_ProjectRoot.string());
	}

	const std::filesystem::path& FileSystem::GetEngineRoot()
	{
		if (s_EngineRoot.empty())
			FileSystem::Init("Cerberus", "Cerberus-Editor");
		return s_EngineRoot;
	}

	const std::filesystem::path& FileSystem::GetProjectRoot()
	{
		if (s_ProjectRoot.empty())
			FileSystem::Init("Cerberus", "Cerberus-Editor");
		return s_ProjectRoot;
	}

	std::filesystem::path FileSystem::GetEngineShaderDirectory()
	{
		return GetEngineRoot() / "Resources" / "Shaders";
	}

	std::filesystem::path FileSystem::GetProjectShaderDirectory()
	{
		return GetProjectRoot() / "Resources" / "Shaders";
	}

	bool FileSystem::IsVirtualPath(const std::string& filepath)
	{
		std::string normalized = NormalizeSlashes(filepath);
		return normalized.rfind("engine://", 0) == 0 || normalized.rfind("project://", 0) == 0;
	}

	std::filesystem::path FileSystem::ResolvePath(const std::string& filepath)
	{
		std::string normalized = NormalizeSlashes(filepath);

		const std::string enginePrefix = "engine://";
		const std::string projectPrefix = "project://";

		if (normalized.rfind(enginePrefix, 0) == 0)
		{
			std::filesystem::path relative = StripOptionalResourcesPrefix(normalized.substr(enginePrefix.size()));
			return GetEngineRoot() / "Resources" / relative;
		}

		if (normalized.rfind(projectPrefix, 0) == 0)
		{
			std::filesystem::path relative = StripOptionalResourcesPrefix(normalized.substr(projectPrefix.size()));
			return GetProjectRoot() / "Resources" / relative;
		}

		std::filesystem::path path(normalized);

		// Absolute paths do not need remapping.
		if (path.is_absolute())
			return path;

		// Legacy support: keep paths such as "Resources/Shaders/PBR.glsl" working.
		// The editor/project resources are searched first to preserve existing behavior,
		// then engine resources are searched for renderer-owned shaders.
		std::array<std::filesystem::path, 4> candidates = {
			GetProjectRoot() / path,
			GetEngineRoot() / path,
			std::filesystem::current_path() / path,
			path
		};

		for (const std::filesystem::path& candidate : candidates)
		{
			if (std::filesystem::exists(candidate))
				return candidate;
		}

		return path;
	}

	std::filesystem::path FileSystem::ResolveFileSystemPath(const std::string& filepath)
	{
		std::filesystem::path path(filepath);

		if (path.is_absolute())
			return path;

		return std::filesystem::absolute(std::filesystem::current_path() / path);
	}

	std::filesystem::path FileSystem::GetRelativePathFromAbsolute(const std::filesystem::path absolutePath)
	{
		std::filesystem::path currentPath = std::filesystem::current_path();
		return std::filesystem::relative(absolutePath, currentPath);
	}
	std::string FileSystem::GetProjectPath()
	{
		return GetProjectRoot().string();
	}
	bool FileSystem::Exists(const std::filesystem::path& filepath)
	{
		return std::filesystem::exists(ResolveFileSystemPath(filepath.string()));
	}
	bool FileSystem::Exists(const std::string& filepath)
	{
		return std::filesystem::exists(ResolvePath(filepath));
	}

	bool FileSystem::CreateDirectory(const std::filesystem::path& directory)
	{
		return std::filesystem::create_directories(directory);
	}

	bool FileSystem::CreateDirectory(const std::string& directory)
	{
		return CreateDirectory(std::filesystem::path(directory));
	}

	ProjectConfig& FileSystem::GetProjectConfig()
	{
		static ProjectConfig config;
		return config;

	}

	bool FileSystem::Move(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		if (FileSystem::Exists(newFilepath) || oldFilepath == newFilepath)
			return false;

		std::filesystem::rename(oldFilepath, newFilepath);
		return true;
	}

	bool FileSystem::Copy(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		if (FileSystem::Exists(newFilepath) || oldFilepath == newFilepath)
			return false;

		std::filesystem::copy(oldFilepath, newFilepath);
		return true;
	}

	bool FileSystem::MoveFile(const std::filesystem::path& filepath, const std::filesystem::path& dest)
	{
		return Move(filepath, dest / filepath.filename());
	}

	bool FileSystem::CopyFile(const std::filesystem::path& filepath, const std::filesystem::path& dest)
	{
		return Copy(filepath, dest / filepath.filename());;
	}

	bool FileSystem::Rename(const std::filesystem::path& oldFilepath, const std::filesystem::path& newFilepath)
	{
		return Move(oldFilepath, newFilepath);
	}

	bool FileSystem::RenameFilename(const std::filesystem::path& oldFilepath, const std::string& newName)
	{
		std::filesystem::path newPath = fmt::format("{0}\\{1}{2}", oldFilepath.parent_path().string(), newName, oldFilepath.extension());
		return Rename(oldFilepath, newPath);
	}

	bool FileSystem::DeleteFile(const std::filesystem::path& filepath)
	{
		if (!FileSystem::Exists(filepath))
			return false;

		if (std::filesystem::is_directory(filepath))
			return std::filesystem::remove_all(filepath) > 0;
		return std::filesystem::remove(filepath);
	}

	bool FileSystem::IsDirectory(const std::filesystem::path& filepath)
	{
		return std::filesystem::is_directory(filepath);
	}

	bool FileSystem::ShowFileInExplorer(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

		std::string cmd = fmt::format("explorer.exe /select,\"{0}\"", absolutePath.string());
		system(cmd.c_str());
		return true;
	}

	bool FileSystem::OpenDirectoryInExplorer(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

		ShellExecute(NULL, L"explore", absolutePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
		return true;
	}

	bool FileSystem::OpenExternally(const std::filesystem::path& path)
	{
		auto absolutePath = std::filesystem::canonical(path);
		if (!Exists(absolutePath))
			return false;

		ShellExecute(NULL, L"open", absolutePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
		return true;
	}
}
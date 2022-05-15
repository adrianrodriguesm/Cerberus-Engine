#include "pch.h"
#include "AssetManager.h"
#include "Utils/FileSystem.h"
#include "Asset/AssetExtension.h"
#include "Utils/StringUtils.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
namespace cerberus
{
	static AssetMetadata s_NullMetadata;

	void AssetManager::Init()
	{
		AssetImporter::Init();
		// Deserialize a the asset registry
		LoadAssetRegistry();
		// Process the assets within the asset directory
		ProcessDirectory(FileSystem::GetProjectConfig().AssetDirectory);
		// Update the asset registry (serialize)
		WriteRegistryToFile();
	}


	void AssetManager::Shutdown()
	{
		WriteRegistryToFile();

		s_AssetRegistry.Clear();
		s_LoadedAssets.clear();
	}


	const AssetMetadata& AssetManager::GetMetadata(AssetHandle handle)
	{
		return GetMetadataInternal(handle);
	}
	const AssetMetadata& AssetManager::GetMetadata(const std::filesystem::path& filepath)
	{
		if (s_AssetRegistry.Contains(filepath))
			return s_AssetRegistry[filepath];

		return s_NullMetadata;
	}
	AssetHandle AssetManager::ImportAsset(const std::filesystem::path& filepath)
	{
		if (s_AssetRegistry.Contains(filepath))
			return s_AssetRegistry[filepath].Handle;

		AssetType type = GetAssetTypeFromPath(filepath);
		if (type == AssetType::None)
			return 0;

		AssetMetadata metadata;
		metadata.Handle = AssetHandle();
		metadata.FilePath = filepath;
		metadata.Type = type;
		s_AssetRegistry[metadata.FilePath] = metadata;

		return metadata.Handle;
	}
	AssetHandle AssetManager::GetAssetHandleFromFilePath(const std::filesystem::path& filepath)
	{
		if (s_AssetRegistry.Contains(filepath))
			return s_AssetRegistry[filepath].Handle;

		return AssetHandle();
	}
	void AssetManager::ProcessDirectory(const std::filesystem::path& directoryPath)
	{
		for (auto entry : std::filesystem::directory_iterator(directoryPath))
		{
			if (entry.is_directory())
				ProcessDirectory(entry.path());
			else
				ImportAsset(entry.path());
		}
	}

	AssetHandle AssetManager::ImportAsset(const std::filesystem::path& srcAssetFilepath, const std::filesystem::path& dstFilepath)
	{
		if (dstFilepath.root_path() != FileSystem::GetProjectConfig().AssetDirectory)
			return 0;

		bool copied = FileSystem::CopyFile(srcAssetFilepath, dstFilepath);
		if (copied)
			return ImportAsset(dstFilepath / srcAssetFilepath.filename());
		else
			return 0;
	}
	AssetMetadata& AssetManager::GetMetadataInternal(AssetHandle handle)
	{
		for (auto& [filepath, metadata] : s_AssetRegistry)
		{
			if (metadata.Handle == handle)
				return metadata;
		}

		return s_NullMetadata;
	}

	void AssetManager::WriteRegistryToFile()
	{
		// Sort assets by UUID to make project managment easier
		struct AssetRegistryEntry
		{
			std::string FilePath;
			AssetType Type;
		};
		std::map<UUID, AssetRegistryEntry> sortedMap;
		for (auto& [filepath, metadata] : s_AssetRegistry)
		{
			/**/
			if (!FileSystem::Exists(metadata.FilePath))
				continue;
			/**/
			std::string pathToSerialize = metadata.FilePath.string();
			std::replace(pathToSerialize.begin(), pathToSerialize.end(), '\\', '/');
			sortedMap[metadata.Handle] = { pathToSerialize, metadata.Type };
		}

		NV_CORE_INFO("[AssetManager] serializing asset registry with {0} entries", sortedMap.size());
		// Serialization
		YAML::Emitter out;
		out << YAML::BeginMap;

		out << YAML::Key << "Assets" << YAML::BeginSeq;
		for (auto& [handle, entry] : sortedMap)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Handle" << YAML::Value << handle;
			out << YAML::Key << "FilePath" << YAML::Value << entry.FilePath;
			out << YAML::Key << "Type" << YAML::Value << utils::AssetTypeToString(entry.Type);
			out << YAML::EndMap;
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		const std::string& assetRegistryPath = FileSystem::GetProjectConfig().AssetRegistryPath;
		std::ofstream fout(assetRegistryPath);
		fout << out.c_str();

	}
	void AssetManager::LoadAssetRegistry()
	{
		NV_CORE_INFO("[AssetManager] Loading Asset Registry");

		const auto& assetRegistryPath = FileSystem::GetProjectConfig().AssetRegistryPath;
		if (!FileSystem::Exists(assetRegistryPath))
			return;

		std::ifstream stream(assetRegistryPath);
		CORE_ASSERT(stream, "Invalid asset registry path");
		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		auto handles = data["Assets"];
		if (!handles)
		{
			NV_CORE_CRITICAL("[AssetManager] Asset Registry appears to be corrupted!");
			return;
		}

		// Iterate over the assets
		for (auto entry : handles)
		{
			// Get AssetMetadata from the AssetRegistry file
			std::string filepath = entry["FilePath"].as<std::string>();
			AssetMetadata metadata;
			metadata.Handle = entry["Handle"].as<uint64_t>();
			metadata.FilePath = filepath;
			metadata.Type = (AssetType)utils::AssetTypeFromString(entry["Type"].as<std::string>());
			// Invalid AssetType
			if (metadata.Type == AssetType::None)
				continue;

			// TODO: Handle these two cases
			if (!FileSystem::Exists(metadata.FilePath.string()))
			{
				NV_CORE_WARN("[AssetManager] Missing asset '{0}' detected in registry file", metadata.FilePath);
				continue;
			}
			else if (metadata.Handle == 0)
			{
				NV_CORE_WARN("[AssetManager] AssetHandle for {0} is 0, this shouldn't happen.", metadata.FilePath);
				continue;
			}
			s_AssetRegistry[metadata.FilePath] = metadata;
		}
		NV_CORE_INFO("[AssetManager] Loaded {0} asset entries", s_AssetRegistry.Count());
	}

	AssetType AssetManager::GetAssetTypeFromExtension(const std::string& extension)
	{
		std::string ext = utils::ToLower(extension);
		if (s_AssetExtensionMap.find(ext) == s_AssetExtensionMap.end())
			return AssetType::None;

		return s_AssetExtensionMap.at(ext.c_str());
	}
	AssetType AssetManager::GetAssetTypeFromPath(const std::filesystem::path& path)
	{
		return GetAssetTypeFromExtension(path.extension().string());
	}

	void AssetManager::OnAssetRenamed(AssetHandle assetHandle, const std::filesystem::path& newFilePath)
	{
		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return;

		s_AssetRegistry.Remove(metadata.FilePath);
		metadata.FilePath = newFilePath;
		s_AssetRegistry[metadata.FilePath] = metadata;
		WriteRegistryToFile();
	}

	void AssetManager::OnAssetMoved(AssetHandle assetHandle, const std::filesystem::path& destinationPath)
	{
		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return;

		s_AssetRegistry.Remove(metadata.FilePath);
		metadata.FilePath = destinationPath / metadata.FilePath.filename();
		s_AssetRegistry[metadata.FilePath] = metadata;

		WriteRegistryToFile();
	}

	void AssetManager::OnAssetDeleted(AssetHandle assetHandle)
	{
		AssetMetadata metadata = GetMetadata(assetHandle);
		if (!metadata.IsValid())
			return;

		s_AssetRegistry.Remove(metadata.FilePath);
		s_LoadedAssets.erase(assetHandle);
		WriteRegistryToFile();
	}
}
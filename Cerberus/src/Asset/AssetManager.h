#pragma once
#include "Asset/Asset.h"
#include "AssetRegistry.h"
#include "Asset/AssetImporter.h"
#include "Utils/FileSystem.h"
namespace cerberus
{
	class AssetManager
	{
	public:
		static void Init();
		static void Shutdown();

		static const AssetMetadata& GetMetadata(AssetHandle handle);
		static const AssetMetadata& GetMetadata(const std::filesystem::path& filepath);
		static const AssetMetadata& GetMetadata(const Ref<Asset>& asset) { return GetMetadata(asset->Handle); }
		
		static AssetHandle ImportAsset(const std::filesystem::path& filepath);
		static AssetHandle ImportAsset(const std::filesystem::path& srcAssetFilepath, const std::filesystem::path& dstAssetFilepath);
		static void ProcessDirectory(const std::filesystem::path& directoryPath);

		static AssetHandle GetAssetHandleFromFilePath(const std::filesystem::path& filepath);
		static bool IsAssetHandleValid(AssetHandle assetHandle) { return GetMetadata(assetHandle).IsValid(); }

		static AssetType GetAssetTypeFromExtension(const std::string& extension);
		static AssetType GetAssetTypeFromPath(const std::filesystem::path& path);

		static void OnAssetRenamed(AssetHandle assetHandle, const std::filesystem::path& newFilePath);
		static void OnAssetMoved(AssetHandle assetHandle, const std::filesystem::path& destinationPath);
		static void OnAssetDeleted(AssetHandle assetHandle);

		static std::filesystem::path GetAssetPath() { static std::filesystem::path assetPath = "Resources"; return assetPath; }

		template<typename T, typename... Args>
		static Ref<T> OverrideAsset(const AssetHandle& assetHandle, Args&&... args)
		{
			static_assert(std::is_base_of<Asset, T>::value, "OverrideAsset only works for types derived from Asset");

			auto& metadata = GetMetadataInternal(assetHandle);
			if (!metadata.IsValid())
				return nullptr;

			Ref<T> asset = CreateRef<T>(std::forward<Args>(args)...);
			asset->Handle = metadata.Handle;
			s_LoadedAssets[asset->Handle] = asset;
			// Serialize Asset
			AssetImporter::Serialize(metadata, asset);

			return asset;
		}

		template<typename T, typename... Args>
		static Ref<T> CreateNewAsset(const std::string& filename, const std::string& directoryPath, Args&&... args)
		{
			static_assert(std::is_base_of<Asset, T>::value, "CreateNewAsset only works for types derived from Asset");

			AssetMetadata metadata;
			metadata.Handle = AssetHandle();
			if (directoryPath.empty() || directoryPath == ".")
				metadata.FilePath = filename;
			else
				metadata.FilePath = directoryPath + "/" + filename;

			metadata.IsDataLoaded = true;
			metadata.Type = T::GetStaticType();

			if(std::filesystem::exists(metadata.FilePath))
			{
				bool foundAvailableFileName = false;
				int current = 1;

				while (!foundAvailableFileName)
				{
					std::string nextFilePath = directoryPath + "/" + metadata.FilePath.stem().string();
					if (current < 10)
						nextFilePath += " (0" + std::to_string(current) + ")";
					else
						nextFilePath += " (" + std::to_string(current) + ")";
					nextFilePath += metadata.FilePath.extension().string();

					if (!std::filesystem::exists(nextFilePath))
					{
						foundAvailableFileName = true;
						metadata.FilePath = nextFilePath;
						break;
					}

					current++;
				}
			}
			else
			{
				FileSystem::CreateDirectory(directoryPath);
			}

			s_AssetRegistry[metadata.FilePath] = metadata;
			// Serialize new entry
			WriteRegistryToFile();

			Ref<T> asset = CreateRef<T>(std::forward<Args>(args)...);
			asset->Handle = metadata.Handle;
			s_LoadedAssets[asset->Handle] = asset;
			// Serialize Asset
			AssetImporter::Serialize(metadata, asset);

			return asset;
		}


		template<typename T>
		static Ref<T> GetAsset(AssetHandle assetHandle)
		{
			auto& metadata = GetMetadataInternal(assetHandle);
			if (!metadata.IsValid())
				return nullptr;

			Ref<Asset> asset = nullptr;
			if (!metadata.IsDataLoaded)
			{
				// If the asset is no loaded try to load the data
				metadata.IsDataLoaded = AssetImporter::TryLoadData(metadata, asset);
				if (!metadata.IsDataLoaded)
					return nullptr;

				s_LoadedAssets[assetHandle] = asset;
			}
			else
			{
				asset = s_LoadedAssets[assetHandle];
			}

			return std::dynamic_pointer_cast<T>(asset);
		}

		template<typename T>
		static Ref<T> GetAsset(const std::filesystem::path& filepath)
		{
			return GetAsset<T>(GetAssetHandleFromFilePath(filepath));
		}

		template<typename T>
		static bool LoadMemoryAsset(const Ref<T> asset, const std::string& filename, const std::string& directoryPath)
		{
			static_assert(std::is_base_of<Asset, T>::value, "CreateNewAsset only works for types derived from Asset");

			AssetMetadata metadata;
			metadata.Handle = AssetHandle();
			if (directoryPath.empty() || directoryPath == ".")
				metadata.FilePath = filename;
			else
				metadata.FilePath = directoryPath + "/" + filename;

			metadata.IsDataLoaded = true;
			metadata.Type = T::GetStaticType();

			if (std::filesystem::exists(metadata.FilePath))
			{
				bool foundAvailableFileName = false;
				int current = 1;

				while (!foundAvailableFileName)
				{
					std::string nextFilePath = directoryPath + "/" + metadata.FilePath.stem().string();
					if (current < 10)
						nextFilePath += " (0" + std::to_string(current) + ")";
					else
						nextFilePath += " (" + std::to_string(current) + ")";
					nextFilePath += metadata.FilePath.extension().string();

					if (!std::filesystem::exists(nextFilePath))
					{
						foundAvailableFileName = true;
						metadata.FilePath = nextFilePath;
						break;
					}

					current++;
				}
			}
			else
			{
				FileSystem::CreateDirectory(directoryPath);
			}

			s_AssetRegistry[metadata.FilePath] = metadata;
			// Serialize new entry
			WriteRegistryToFile();

			asset->Handle = metadata.Handle;
			s_LoadedAssets[asset->Handle] = asset;
			// Serialize Asset
			AssetImporter::Serialize(metadata, asset);

			return true;
		}

	private: 
		static AssetMetadata& GetMetadataInternal(AssetHandle handle);
		// Serialize Asset Registry
		static void WriteRegistryToFile();
		// Deserialize Asset Registry
		static void LoadAssetRegistry();
	private:
		// The main diference between the loaded and memory assets is that the Loaded assets are de/serialize
		inline static std::unordered_map<AssetHandle, Ref<Asset>> s_LoadedAssets;
	
		inline static AssetRegistry s_AssetRegistry;
	};
}
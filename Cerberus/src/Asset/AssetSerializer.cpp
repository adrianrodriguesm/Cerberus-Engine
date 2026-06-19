#include "pch.h"
#include "Utils/SerializationUtils.h"
#include "Asset/AssetSerializer.h"
#include "Asset/AssetManager.h"
#include "Renderer/RendererResources.h"
#include "Scene/Entity.h"
#include "Utils/FileSystem.h"
#include "Asset/AssetImporter.h"
#include "Renderer/TextureAsset.h"
#include <fstream>
namespace cerberus
{
	void MeshSourceSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		const auto& filepath = (metadata.FilePath.string() + ".meta");
		YAML::Emitter out;
		out << YAML::BeginMap; // MeshSourceImporter
		out << YAML::Key << "MeshSourceImporter" << YAML::Value << YAML::BeginSeq;
			
				Ref<MeshSource> meshSource = std::dynamic_pointer_cast<MeshSource>(asset);
				const auto& materialAssets = meshSource->GetMaterialsAsset();
				const auto& submeshes = meshSource->GetSubmeshes();
				for (size_t i = 0; i < submeshes.size(); i++)
				{
					const uint32_t materialIndex = submeshes[i].MaterialIndex;
					if (materialIndex >= materialAssets.size() || !materialAssets[materialIndex])
						continue;

					AssetHandle materialHandle = materialAssets[materialIndex]->Handle;
					if (!materialHandle)
						continue;

					out << YAML::BeginMap;
					out << YAML::Key << "SubmeshIndex" << YAML::Value << i;
					out << YAML::Key << "MaterialHandle" << YAML::Value << (uint64_t)materialHandle;
					out << YAML::EndMap;
				}
			
			out << YAML::EndSeq;
		
		out << YAML::EndMap;
		std::ofstream fout(filepath);
		fout << out.c_str();

	}

	static MeshSourceProperties LoadMeshSourceImportSettings(const std::filesystem::path filepath)
	{
		std::filesystem::path metafilepath = (filepath.string() + ".meta");
		// Verify if it is a valid filepath
		std::ifstream stream(metafilepath);
		if (!stream)
			return MeshSourceProperties();
		
		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		if(!data["MeshSourceImporter"])
			return MeshSourceProperties();

		// Get the materials associated with each submesh
		auto submeshMaterials = data["MeshSourceImporter"];

		// Fill the MeshSourceProperties
		MeshSourceProperties meshSourceProp;
		for (auto submesh : submeshMaterials)
		{
			if (!submesh["SubmeshIndex"] || !submesh["MaterialHandle"])
				continue;

			uint32_t submeshIndex = submesh["SubmeshIndex"].as<uint32_t>();
			AssetHandle materialHandle = 0;
			materialHandle = submesh["MaterialHandle"].as<uint64_t>();
			if (materialHandle)
				meshSourceProp.SubmeshMaterials[submeshIndex] = materialHandle;
		}
		return meshSourceProp;
	}

	bool MeshSourceSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		// MeshSource Importer
		MeshSourceProperties meshSourceProperties = LoadMeshSourceImportSettings(metadata.FilePath);
		meshSourceProperties.Filepath = metadata.FilePath.string();
		asset = CreateRef<MeshSource>(meshSourceProperties);
		asset->Handle = metadata.Handle;

		AssetImporter::Serialize(asset);
		return (std::dynamic_pointer_cast<MeshSource>(asset))->GetIndices().size() > 0;
	}

	void MeshSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		Ref<Mesh> mesh = std::dynamic_pointer_cast<Mesh>(asset);
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Mesh";
		{
			out << YAML::BeginMap;
			out << YAML::Key << "MeshSource";
			out << YAML::Value << mesh->GetMeshSource()->Handle;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		auto serializePath = metadata.FilePath;
		NV_CORE_WARN("Serializing to {0}", serializePath.string());
		std::ofstream fout(serializePath);
		CORE_ASSERT(fout.good(), "Invalid Path");
		if (fout.good())
			fout << out.c_str();
	}
	bool MeshSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		auto filepath = metadata.FilePath;
		std::ifstream stream(filepath);
		CORE_ASSERT(stream, "Invalid Filepath!");
		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		if (!data["Mesh"])
			return false;

		YAML::Node rootNode = data["Mesh"];
		if (!rootNode["MeshSource"])
			return false;

		AssetHandle meshSourceHandle = 0;
		meshSourceHandle = rootNode["MeshSource"].as<uint64_t>();

		Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(meshSourceHandle);
		if (!meshSource)
		{
			NV_CORE_ERROR("Invalid mesh source attache to {0}", filepath);
			return false;
		}
		Ref<Mesh> mesh = CreateRef<Mesh>(meshSource);
		mesh->Handle = metadata.Handle;
		asset = mesh;
		return true;
	}

	static void SerializeEntity(YAML::Emitter& out, Entity entity)
	{
		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity" << YAML::Value << entity.GetComponent<IDComponent>().ID;

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap; // TagComponent

			auto& tag = entity.GetComponent<TagComponent>().Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;

			out << YAML::EndMap; // TagComponent
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap; // TransformComponent

			auto& tc = entity.GetComponent<TransformComponent>();
			out << YAML::Key << "Translation" << YAML::Value << tc.GetTranslation();
			out << YAML::Key << "Rotation" << YAML::Value << tc.GetRotation();
			out << YAML::Key << "Scale" << YAML::Value << tc.GetScale();

			out << YAML::EndMap; // TransformComponent
		}

		if (entity.HasComponent<DirectionalLightComponent>())
		{
			out << YAML::Key << "DirectionalLightComponent";
			out << YAML::BeginMap; // DirectionalLightComponent
			auto& directionalLightComponent = entity.GetComponent<DirectionalLightComponent>();
			out << YAML::Key << "Radiance" << YAML::Value << directionalLightComponent.Radiance;
			out << YAML::Key << "Intensity" << YAML::Value << directionalLightComponent.Intensity;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<MeshComponent>())
		{
			out << YAML::Key << "MeshComponent";
			out << YAML::BeginMap; // MeshComponent
			auto& meshComponent = entity.GetComponent<MeshComponent>();
			out << YAML::Key << "AssetHandle" << YAML::Value << meshComponent.MeshHandle;
			auto materialTable = meshComponent.MaterialTable;
			if (materialTable->GetMaterialCount() > 0)
			{
				out << YAML::Key << "MaterialTable" << YAML::Value << YAML::BeginMap; // MaterialTable
				Ref<MaterialAsset> defaultMaterial = RendererResources::GetDefaultPBRMaterial();
				for (uint32_t i = 0; i < materialTable->GetMaterialCount(); i++)
				{
					Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(materialTable->GetMaterial(i));
					AssetHandle handle = (material != defaultMaterial) ? material->Handle : (AssetHandle)0;
					out << YAML::Key << i << YAML::Value << handle;

				}

				out << YAML::EndMap; // MaterialTable
			}
			out << YAML::EndMap;
		}

		if (entity.HasComponent<SkyLightComponent>())
		{
			out << YAML::Key << "SkyLightComponent";
			out << YAML::BeginMap; // SkyLightComponent
			auto& skyLightComponent = entity.GetComponent<SkyLightComponent>();
			out << YAML::Key << "AssetHandle" << YAML::Value << skyLightComponent.EnvironmentHandle;
			out << YAML::Key << "Intensity" << YAML::Value << skyLightComponent.Intensity;
			out << YAML::Key << "SampleLOD" << YAML::Value << skyLightComponent.SampleLOD;
			out << YAML::EndMap;
		}

		out << YAML::EndMap; // Entity
	}

	void SceneSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		auto& filepath = metadata.FilePath;
		Ref<Scene> scene = std::dynamic_pointer_cast<Scene>(asset);
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << scene->m_Name;
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		scene->m_Registry.each([&](auto entityID)
		{
			Entity entity = { entityID, scene.get() };
			if (!entity)
				return;

			SerializeEntity(out, entity);
		});
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	bool SceneSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		const auto& filepath = metadata.FilePath.string();
		YAML::Node data = YAML::LoadFile(filepath);
		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		Ref<Scene> scene = CreateRef<Scene>(sceneName);

		NV_CORE_TRACE("Deserializing scene '{0}'", sceneName);

		auto entities = data["Entities"];
		if (entities)
		{
			for (auto entity : entities)
			{
				uint64_t uuid = entity["Entity"].as<uint64_t>();

				std::string name;
				auto tagComponent = entity["TagComponent"];
				if (tagComponent)
					name = tagComponent["Tag"].as<std::string>();

				NV_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

				Entity deserializedEntity = scene->CreateEntity(name);

				auto transformComponent = entity["TransformComponent"];
				if (transformComponent)
				{
					// Entities always have transforms
					auto& tc = deserializedEntity.GetComponent<TransformComponent>();
					tc.SetTranslation(transformComponent["Translation"].as<glm::vec3>());
					tc.SetRotation(transformComponent["Rotation"].as<glm::vec3>());
					tc.SetScale(transformComponent["Scale"].as<glm::vec3>());
				}

				auto meshComponent = entity["MeshComponent"];
				if (meshComponent)
				{
					auto& mc = deserializedEntity.AddComponent<MeshComponent>();
					mc.MeshHandle = meshComponent["AssetHandle"].as<uint64_t>();
					mc.MaterialTable = CreateRef<MaterialTable>();
					if (meshComponent["MaterialTable"])
					{
						YAML::Node materialTableNode = meshComponent["MaterialTable"];
						Ref<MaterialAsset> defaultPBRMaterial = RendererResources::GetDefaultPBRMaterial();
						for (auto materialEntry : materialTableNode)
						{
							uint32_t index = materialEntry.first.as<uint32_t>();
							AssetHandle materialAssetHandle = materialEntry.second.as<AssetHandle>();
							if (materialAssetHandle && AssetManager::IsAssetHandleValid(materialAssetHandle))
								mc.MaterialTable->SetMaterial(index, materialAssetHandle);
							// OLD_RENDERER
							/** /
							else
								mc.MaterialTable->SetMaterial(index, defaultPBRMaterial);
							/**/
						}
					}

				}

				auto skyLightComponent = entity["SkyLightComponent"];
				if (skyLightComponent)
				{
#if OLD_RENDERER
					auto& slc = deserializedEntity.AddComponent<SkyLightComponent>();
					
					AssetHandle assetHandle = skyLightComponent["AssetHandle"].as<AssetHandle>();
					slc.EnvironmentHandle = AssetManager::GetAsset<EnvironmentMap>(assetHandle)->Handle;
					slc.Intensity = skyLightComponent["Intensity"].as<float>();
					slc.SampleLOD = skyLightComponent["SampleLOD"].as<uint32_t>();
#endif
				}

				auto directionalLightComponent = entity["DirectionalLightComponent"];
				if (directionalLightComponent)
				{
					auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
					dlc.Intensity = directionalLightComponent["Intensity"].as<float>();
					dlc.Radiance = directionalLightComponent["Radiance"].as<glm::vec3>();
				}
			}
		}

		scene->SetName(metadata.FilePath.stem().string());
		scene->Handle = metadata.Handle;
		asset = scene;
		return true;
	}


	bool EnvironmentMapSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
#if OLD_RENDERER
		auto& filepath = metadata.FilePath;

		Ref<EnvironmentMap> environment = Renderer::CreateEnvironmentMap(filepath.string());
		environment->Handle = metadata.Handle;
		asset = environment;

		return true;
#else
		return false;

#endif
	}
	void MaterialSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		Ref<MaterialAsset> material = std::dynamic_pointer_cast<MaterialAsset>(asset);

		YAML::Emitter out;
		out << YAML::BeginMap; // Material
		out << YAML::Key << "Material";
		{
			out << YAML::BeginMap;

			out << YAML::Key << "AlbedoColor" << YAML::Value << material->GetAlbedoColor();
			out << YAML::Key << "UseNormalMap" << YAML::Value << material->IsUsingNormalMap();
			out << YAML::Key << "Metalness" << YAML::Value << material->GetMetalness();
			out << YAML::Key << "Roughness" << YAML::Value << material->GetRoughness();
			out << YAML::Key << "Emission" << YAML::Value << material->GetEmission();

			// Texture asset handles. Runtime GPU texture handles / bindless indices are created later by RenderScene/MaterialProxy.
			out << YAML::Key << "AlbedoMapHandle" << YAML::Value << (uint64_t)material->GetAlbedoMapHandle();
			out << YAML::Key << "NormalMapHandle" << YAML::Value << (uint64_t)material->GetNormalMapHandle();
			out << YAML::Key << "MetalnessMapHandle" << YAML::Value << (uint64_t)material->GetMetalnessMapHandle();
			out << YAML::Key << "RoughnessMapHandle" << YAML::Value << (uint64_t)material->GetRoughnessMapHandle();

			out << YAML::EndMap;
		}
		out << YAML::EndMap; // Material

		std::ofstream fout(metadata.FilePath);
		fout << out.c_str();
	}

	bool MaterialSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		const auto& filepath = metadata.FilePath.string();
		YAML::Node data = YAML::LoadFile(filepath);
		if (!data["Material"])
			return false;

		Ref<MaterialAsset> material = CreateRef<MaterialAsset>();
		YAML::Node rootNode = data["Material"];

		if (rootNode["AlbedoColor"])
			material->SetAlbedoColor(rootNode["AlbedoColor"].as<glm::vec3>());

		if (rootNode["UseNormalMap"])
			material->SetUseNormalMap(rootNode["UseNormalMap"].as<bool>());

		if (rootNode["Metalness"])
			material->SetMetalness(rootNode["Metalness"].as<float>());

		if (rootNode["Roughness"])
			material->SetRoughness(rootNode["Roughness"].as<float>());

		if (rootNode["Emission"])
			material->SetEmission(rootNode["Emission"].as<float>());

		if (rootNode["AlbedoMapHandle"])
			material->SetAlbedoMap((AssetHandle)rootNode["AlbedoMapHandle"].as<uint64_t>());

		if (rootNode["NormalMapHandle"])
			material->SetNormalMap((AssetHandle)rootNode["NormalMapHandle"].as<uint64_t>());

		if (rootNode["MetalnessMapHandle"])
			material->SetMetalnessMap((AssetHandle)rootNode["MetalnessMapHandle"].as<uint64_t>());

		if (rootNode["RoughnessMapHandle"])
			material->SetRoughnessMap((AssetHandle)rootNode["RoughnessMapHandle"].as<uint64_t>());

		material->Handle = metadata.Handle;
		asset = material;
		return true;
	}

	void TextureSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		Ref<Texture2D> texture = std::dynamic_pointer_cast<Texture2D>(asset);
		const auto& textureProp = texture->GetTextureProperties();
		const auto& filepath = (metadata.FilePath.string() + ".meta");
		YAML::Emitter out;
		out << YAML::BeginMap; // Texture2D
		out << YAML::Key << "TextureImporter";
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << textureProp.DebugName;
			out << YAML::Key << "GeneratesMips" << YAML::Value << textureProp.GenerateMips;
			out << YAML::Key << "SRGB" << YAML::Value << textureProp.SRGB;
			out << YAML::Key << "SamplerWrap" << YAML::Value << utils::SamplerWrapToString(textureProp.SamplerWrap);
			out << YAML::Key << "SamplerFilter" << YAML::Value << utils::SamplerFilterToString(textureProp.SamplerFilter);
			out << YAML::Key << "Anisotropy" << YAML::Value << textureProp.Anisotropy;
			out << YAML::EndMap;
		}
		out << YAML::EndMap; // Texture2D

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	static TextureProperties LoadTextureImportSettings(const std::filesystem::path filepath)
	{
		std::filesystem::path metafilepath = (filepath.string() + ".meta");
		// Verify if it is a valid filepath
		std::ifstream stream(metafilepath);
		if (!stream || !FileSystem::Exists(metafilepath))
			return TextureProperties();

		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		if (!data["TextureImporter"])
			return TextureProperties();

		YAML::Node rootNode = data["TextureImporter"];
		TextureProperties properties;
		properties.DebugName = rootNode["Name"].as<std::string>();
		properties.GenerateMips = rootNode["GeneratesMips"].as<bool>();
		properties.SRGB = rootNode["SRGB"].as<bool>();
		properties.SamplerWrap = utils::SamplerWrapFromString(rootNode["SamplerWrap"].as<std::string>());
		properties.SamplerFilter = utils::SamplerFilterFromString(rootNode["SamplerFilter"].as<std::string>());
		properties.Anisotropy = rootNode["Anisotropy"].as<bool>();

		return properties;
	}

	bool TextureSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
#if OLD_RENDERER
		TextureProperties textureProp = LoadTextureImportSettings(metadata.FilePath);

		Ref<TextureAsset> texture = CreateRef<TextureAsset>(metadata.FilePath.string(), textureProp);

		texture->Handle = metadata.Handle;
		asset = texture;

		return texture->Loaded();
#else
		return false;
#endif
	}
}
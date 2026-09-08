#include "pch.h"
#include "Utils/SerializationUtils.h"
#include "Asset/AssetSerializer.h"
#include "Asset/AssetManager.h"
#include "Renderer/Core/RendererResources.h"
#include "Scene/Entity.h"
#include "Utils/FileSystem.h"
#include "Asset/AssetImporter.h"
#include "Asset/Assets/TextureAsset.h"
#include "Renderer/Core/Renderer.h"
#include <fstream>
namespace cerberus
{
	void MeshSourceSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		const auto& filepath = (metadata.FilePath.string() + ".meta");
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "MeshSourceImporter" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "Version" << YAML::Value << 2;
		out << YAML::Key << "Materials" << YAML::Value << YAML::BeginSeq;

		Ref<MeshSource> meshSource = std::dynamic_pointer_cast<MeshSource>(asset);
		const auto& materialAssets = meshSource->GetMaterialsAsset();
		for (uint32_t materialSlot = 0; materialSlot < materialAssets.size(); materialSlot++)
		{
			if (!materialAssets[materialSlot] || !materialAssets[materialSlot]->Handle)
				continue;

			out << YAML::BeginMap;
			out << YAML::Key << "MaterialSlot" << YAML::Value << materialSlot;
			out << YAML::Key << "MaterialHandle" << YAML::Value << (uint64_t)materialAssets[materialSlot]->Handle;
			out << YAML::EndMap;
		}

		out << YAML::EndSeq;
		out << YAML::EndMap;
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

		const YAML::Node importerNode = data["MeshSourceImporter"];
		MeshSourceProperties meshSourceProp;

		// Version 2 persists the complete Assimp material table. This includes
		// material slots that are not currently referenced by a submesh.
		if (importerNode.IsMap())
		{
			const YAML::Node materials = importerNode["Materials"];
			if (materials && materials.IsSequence())
			{
				for (const auto& material : materials)
				{
					if (!material["MaterialSlot"] || !material["MaterialHandle"])
						continue;

					const uint32_t materialSlot = material["MaterialSlot"].as<uint32_t>();
					const AssetHandle materialHandle = material["MaterialHandle"].as<uint64_t>();
					if (materialHandle)
						meshSourceProp.MaterialSlots[materialSlot] = materialHandle;
				}
			}
		}
		// Version 1 stored only submeshes. Keep reading it so existing projects
		// migrate automatically when TryToLoadData serializes the MeshSource.
		else if (importerNode.IsSequence())
		{
			for (const auto& submesh : importerNode)
			{
				if (!submesh["SubmeshIndex"] || !submesh["MaterialHandle"])
					continue;

				const uint32_t submeshIndex = submesh["SubmeshIndex"].as<uint32_t>();
				const AssetHandle materialHandle = submesh["MaterialHandle"].as<uint64_t>();
				if (materialHandle)
					meshSourceProp.SubmeshMaterials[submeshIndex] = materialHandle;
			}
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

		{
			auto& relationship = entity.GetComponent<RelationshipComponent>();
			out << YAML::Key << "RelationshipComponent";
			out << YAML::BeginMap; // RelationshipComponent
			out << YAML::Key << "Parent" << YAML::Value << (uint64_t)relationship.Parent;
			out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
			for (UUID childID : relationship.Children)
				out << (uint64_t)childID;
			out << YAML::EndSeq;
			out << YAML::EndMap; // RelationshipComponent
		}

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
			if (meshComponent.SubmeshIndex != -1)
				out << YAML::Key << "SubmeshIndex" << YAML::Value << meshComponent.SubmeshIndex;
			auto materialTable = meshComponent.MaterialTable;
			if (materialTable->GetMaterialCount() > 0)
			{
				out << YAML::Key << "MaterialTable" << YAML::Value << YAML::BeginMap; // MaterialTable
				Ref<MaterialAsset> defaultMaterial = RendererResources::GetDefaultPBRMaterial();
				for (uint32_t i = 0; i < materialTable->GetMaterialCount(); i++)
				{
					AssetHandle handle = 0;
					if (materialTable->HasMaterial(i))
					{
						const AssetHandle sourceHandle = materialTable->GetMaterial(i);
						Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(sourceHandle);
						if (material && material != defaultMaterial)
							handle = material->Handle;
					}
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
		Ref<Scene> scene = std::dynamic_pointer_cast<Scene>(asset);
		if (!scene)
		{
			NV_CORE_ERROR("Cannot serialize a null or non-Scene asset");
			return;
		}

		const std::filesystem::path filepath = FileSystem::ResolveProjectPath(metadata.FilePath);
		if (filepath.empty())
		{
			NV_CORE_ERROR("Cannot serialize scene '{0}': invalid asset path", scene->m_Name);
			return;
		}

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << scene->m_Name;
		out << YAML::Key << "Renderer" << YAML::Value << std::string(SceneRendererTypeToString(scene->m_RendererType));
		if (scene->m_HasEditorCameraPose)
		{
			out << YAML::Key << "EditorCamera" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Position" << YAML::Value << scene->m_EditorCameraPosition;
			out << YAML::Key << "Yaw" << YAML::Value << scene->m_EditorCameraYaw;
			out << YAML::Key << "Pitch" << YAML::Value << scene->m_EditorCameraPitch;
			out << YAML::EndMap;
		}
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

		std::error_code directoryError;
		if (filepath.has_parent_path())
			std::filesystem::create_directories(filepath.parent_path(), directoryError);
		if (directoryError)
		{
			NV_CORE_ERROR("Cannot create scene directory '{0}': {1}",
				filepath.parent_path().string(), directoryError.message());
			return;
		}

		std::ofstream fout(filepath, std::ios::out | std::ios::trunc);
		if (!fout.is_open())
		{
			NV_CORE_ERROR("Cannot open scene '{0}' for serialization", filepath.string());
			return;
		}

		fout << out.c_str();
		fout.flush();
		if (!fout.good())
		{
			NV_CORE_ERROR("Failed to serialize scene '{0}'", filepath.string());
			return;
		}

		NV_CORE_INFO("Serialized scene '{0}' to '{1}'", scene->m_Name, filepath.string());
	}

	bool SceneSerializer::TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset)
	{
		const std::filesystem::path filepath = FileSystem::ResolveProjectPath(metadata.FilePath);
		YAML::Node data = YAML::LoadFile(filepath.string());
		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		Ref<Scene> scene = CreateRef<Scene>(sceneName);
		if (data["Renderer"])
			scene->m_RendererType = SceneRendererTypeFromString(data["Renderer"].as<std::string>());

		if (auto editorCamera = data["EditorCamera"])
		{
			scene->m_EditorCameraPosition = editorCamera["Position"].as<glm::vec3>(scene->m_EditorCameraPosition);
			scene->m_EditorCameraYaw = editorCamera["Yaw"].as<float>(scene->m_EditorCameraYaw);
			scene->m_EditorCameraPitch = editorCamera["Pitch"].as<float>(scene->m_EditorCameraPitch);
			scene->m_HasEditorCameraPose = true;
		}

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

				// CreateEntityWithUUID (not CreateEntity + overwriting the ID
				// afterward) so Scene::m_EntityMap is keyed by the real UUID from
				// the moment the entity exists -- RelationshipComponent below
				// references other entities by this same UUID.
				Entity deserializedEntity = scene->CreateEntityWithUUID(UUID(uuid), name);

				auto relationshipComponent = entity["RelationshipComponent"];
				if (relationshipComponent)
				{
					auto& rc = deserializedEntity.GetComponent<RelationshipComponent>();
					if (relationshipComponent["Parent"])
						rc.Parent = UUID(relationshipComponent["Parent"].as<uint64_t>());
					if (relationshipComponent["Children"])
						for (auto childNode : relationshipComponent["Children"])
							rc.Children.push_back(UUID(childNode.as<uint64_t>()));
				}

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
					mc.SubmeshIndex = meshComponent["SubmeshIndex"] ? meshComponent["SubmeshIndex"].as<int32_t>() : -1;
					mc.MaterialTable = CreateRef<MaterialTable>();
					if (meshComponent["MaterialTable"])
					{
						YAML::Node materialTableNode = meshComponent["MaterialTable"];
						for (auto materialEntry : materialTableNode)
						{
							uint32_t index = materialEntry.first.as<uint32_t>();
							AssetHandle materialAssetHandle = materialEntry.second.as<AssetHandle>();
							if (materialAssetHandle && AssetManager::IsAssetHandleValid(materialAssetHandle))
								mc.MaterialTable->SetMaterial(index, materialAssetHandle);
							else
								mc.MaterialTable->SetMaterial(index, 0);
						}
					}

				}

				auto skyLightComponent = entity["SkyLightComponent"];
				if (skyLightComponent)
				{
					auto& slc = deserializedEntity.AddComponent<SkyLightComponent>();

					const AssetHandle assetHandle = skyLightComponent["AssetHandle"]
						? skyLightComponent["AssetHandle"].as<AssetHandle>()
						: AssetHandle(0);
					slc.EnvironmentHandle = AssetManager::IsAssetHandleValid(assetHandle)
						? assetHandle
						: AssetHandle(0);
					if (skyLightComponent["Intensity"])
						slc.Intensity = skyLightComponent["Intensity"].as<float>();

					if (skyLightComponent["SampleLOD"])
						slc.SampleLOD = skyLightComponent["SampleLOD"].as<uint32_t>();
				}

				auto directionalLightComponent = entity["DirectionalLightComponent"];
				if (directionalLightComponent)
				{
					auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
					if (directionalLightComponent["Intensity"])
						dlc.Intensity = directionalLightComponent["Intensity"].as<float>();

					if (directionalLightComponent["Radiance"])
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
		const auto& filepath = metadata.FilePath;

		Ref<EnvironmentMap> environment = Renderer::CreateEnvironmentMap(filepath.string());
		if (!environment)
			return false;

		environment->Handle = metadata.Handle;
		asset = environment;

		return true;
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

		Ref<MaterialAsset> material = CreateRef<MaterialAsset>(metadata.FilePath.stem().string());
		YAML::Node rootNode = data["Material"];

		if (rootNode["AlbedoColor"])
			material->SetAlbedoColor(rootNode["AlbedoColor"].as<glm::vec3>());

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

		// SetNormalMap enables normal sampling for newly assigned textures. Restore
		// the serialized user toggle afterwards so a present-but-disabled normal map
		// remains disabled across save/load.
		if (rootNode["UseNormalMap"])
			material->SetUseNormalMap(rootNode["UseNormalMap"].as<bool>());

		material->Handle = metadata.Handle;
		asset = material;
		return true;
	}

	void TextureSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset)
	{
		Ref<TextureAsset> texture = std::dynamic_pointer_cast<TextureAsset>(asset);
		CORE_ASSERT(texture, "TextureSerializer received a non-TextureAsset resource");
		const TextureMetadata& textureMetadata = texture->GetTextureMetadata();
		const auto& filepath = (metadata.FilePath.string() + ".meta");
		YAML::Emitter out;
		out << YAML::BeginMap; // Texture2D
		out << YAML::Key << "TextureImporter";
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << textureMetadata.DebugName;
			out << YAML::Key << "GeneratesMips" << YAML::Value << textureMetadata.GenerateMips;
			out << YAML::Key << "SRGB" << YAML::Value << textureMetadata.SRGB;
			out << YAML::Key << "SamplerWrap" << YAML::Value << utils::SamplerWrapToString(textureMetadata.SamplerWrap);
			out << YAML::Key << "SamplerFilter" << YAML::Value << utils::SamplerFilterToString(textureMetadata.SamplerFilter);
			out << YAML::Key << "Anisotropy" << YAML::Value << textureMetadata.Anisotropy;
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
		TextureProperties textureProp = LoadTextureImportSettings(metadata.FilePath);

		TextureMetadata textureMetadata;
		textureMetadata.DebugName = textureProp.DebugName == "None"
			? metadata.FilePath.stem().string()
			: textureProp.DebugName;
		textureMetadata.FilePath = metadata.FilePath;
		textureMetadata.TextureType = TextureType::Texture2D;
		textureMetadata.SamplerWrap = textureProp.SamplerWrap;
		textureMetadata.SamplerFilter = textureProp.SamplerFilter;
		textureMetadata.GenerateMips = textureProp.GenerateMips;
		textureMetadata.SRGB = textureProp.SRGB;
		textureMetadata.Storage = textureProp.Storage;
		textureMetadata.Anisotropy = textureProp.Anisotropy;

		Ref<TextureAsset> texture = CreateRef<TextureAsset>(textureMetadata);

		texture->Handle = metadata.Handle;
		asset = texture;

		return texture->Loaded();
	}
}

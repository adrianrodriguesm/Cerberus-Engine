#include "pch.h"
#include "MeshAsset.h"

#include "Asset/AssetManager.h"

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unordered_map>

#define MESH_DEBUG_LOG 1
#if MESH_DEBUG_LOG
#define NV_MESH_LOG(...) NV_CORE_TRACE(__VA_ARGS__)
#else
#define HZ_MESH_LOG(...)
#endif

namespace cerberus
{
	static glm::mat4 Mat4FromAssimpMat4(const aiMatrix4x4& matrix)
	{
		glm::mat4 result;
		//the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
		result[0][0] = matrix.a1; result[1][0] = matrix.a2; result[2][0] = matrix.a3; result[3][0] = matrix.a4;
		result[0][1] = matrix.b1; result[1][1] = matrix.b2; result[2][1] = matrix.b3; result[3][1] = matrix.b4;
		result[0][2] = matrix.c1; result[1][2] = matrix.c2; result[2][2] = matrix.c3; result[3][2] = matrix.c4;
		result[0][3] = matrix.d1; result[1][3] = matrix.d2; result[2][3] = matrix.d3; result[3][3] = matrix.d4;
		return result;
	}

	static const uint32_t s_MeshImportFlags =
		aiProcess_CalcTangentSpace |        // Create binormals/tangents just in case
		aiProcess_Triangulate |             // Make sure we're triangles
		aiProcess_SortByPType |             // Split meshes by primitive type
		aiProcess_GenNormals |              // Make sure we have legit normals
		aiProcess_GenUVCoords |             // Convert UVs if required 
		aiProcess_OptimizeMeshes |          // Batch draws where possible
		aiProcess_JoinIdenticalVertices |
		aiProcess_ValidateDataStructure;    // Validation

	struct LogStream : public Assimp::LogStream
	{
		static void Initialize()
		{
			if (Assimp::DefaultLogger::isNullLogger())
			{
				Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
				Assimp::DefaultLogger::get()->attachStream(new LogStream, Assimp::Logger::Err | Assimp::Logger::Warn);
			}
		}

		virtual void write(const char* message) override
		{
			NV_CORE_ERROR("Assimp error: {0}", message);
		}
	};

	MeshSource::MeshSource(const MeshSourceProperties& properties)
		: m_Properties(properties)
	{
		LogStream::Initialize();

		m_Importer = CreateScope<Assimp::Importer>();
		const aiScene* scene = m_Importer->ReadFile(m_Properties.Filepath.string(), s_MeshImportFlags);

		if (!scene || !scene->HasMeshes())
		{
			NV_CORE_ERROR("Failed to load mesh file: {0}", m_Properties.Filepath);
			return;
		}

		m_Scene = scene;

		LoadGeometry();
		TraverseNodes(scene->mRootNode);
		LoadMaterials();
	}

	MeshSource::~MeshSource()
	{
		m_Indices.clear();
		m_Vertices.clear();
		m_Submeshes.clear();
	}

	void MeshSource::LoadGeometry()
	{
		m_Submeshes.reserve(m_Scene->mNumMeshes);
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;
		for (uint32_t meshIndex = 0; meshIndex < m_Scene->mNumMeshes; meshIndex++)
		{
			aiMesh* mesh = m_Scene->mMeshes[meshIndex];

			Submesh& submesh = m_Submeshes.emplace_back();
			submesh.BaseVertex = vertexCount;
			submesh.BaseIndex = indexCount;
			submesh.MaterialIndex = mesh->mMaterialIndex;
			submesh.VertexCount = mesh->mNumVertices;
			submesh.IndexCount = mesh->mNumFaces * 3; // Each face is a triangle
			submesh.MeshName = mesh->mName.C_Str();

			// Update Vertex and Index count for the next mesh
			vertexCount += submesh.VertexCount;
			indexCount += submesh.IndexCount;

			CORE_ASSERT(mesh->HasPositions(), "Meshes require positions.");
			CORE_ASSERT(mesh->HasNormals(), "Meshes require normals.");

			// Vertex
			for (uint32_t i = 0; i < mesh->mNumVertices; i++)
			{
				Vertex vertex{};
				vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
				vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };

				if (mesh->HasTangentsAndBitangents())
				{
					vertex.Tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
					vertex.Binormal = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
				}
				// Set tex coords (if they exist)
				// The zero represent the first set of texture coords (a mesh can have multiple tex coords)
				if (mesh->HasTextureCoords(0))
					vertex.Texcoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };

				m_Vertices.push_back(vertex);
			}
			
			// Indices
			for (uint32_t i = 0; i < mesh->mNumFaces; i++)
			{
				CORE_ASSERT(mesh->mFaces[i].mNumIndices == 3, "Must have 3 indices.");
				Index index = { mesh->mFaces[i].mIndices[0], mesh->mFaces[i].mIndices[1], mesh->mFaces[i].mIndices[2] };
				m_Indices.push_back(index);
			}
		}

	}

	void MeshSource::TraverseNodes(aiNode* node, const glm::mat4& parentTransform, uint32_t level)
	{
		// Calculate current node transform
		glm::mat4 transform = parentTransform * Mat4FromAssimpMat4(node->mTransformation);

		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			uint32_t mesh = node->mMeshes[i];
			auto& submesh = m_Submeshes[mesh];
			submesh.NodeName = node->mName.C_Str();
			submesh.Transform = transform;
		}
		// Traverse Child Nodes
		for (uint32_t i = 0; i < node->mNumChildren; i++)
			TraverseNodes(node->mChildren[i], transform, level + 1);
	}

	void MeshSource::LoadMaterials()
	{
		std::unordered_map<std::string, std::filesystem::path> textureFilesByName;
		std::error_code textureScanError;
		for (std::filesystem::recursive_directory_iterator it(
			m_Properties.Filepath.parent_path(),
			std::filesystem::directory_options::skip_permission_denied,
			textureScanError), end; it != end && !textureScanError; it.increment(textureScanError))
		{
			if (!it->is_regular_file())
				continue;
			std::string filename = it->path().filename().string();
			std::transform(filename.begin(), filename.end(), filename.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			textureFilesByName.try_emplace(std::move(filename), it->path());
		}

		auto getMaterialSlotCount = [this]() -> uint32_t
		{
			uint32_t materialSlotCount = m_Scene && m_Scene->HasMaterials() ? m_Scene->mNumMaterials : 0;

			for (const Submesh& submesh : m_Submeshes)
				materialSlotCount = std::max(materialSlotCount, submesh.MaterialIndex + 1);

			return std::max(materialSlotCount, 1u);
		};

		auto importMaterialTexture = [this, &textureFilesByName](aiMaterial* sourceMaterial, aiTextureType textureType, uint32_t textureIndex = 0) -> AssetHandle
		{
			if (!sourceMaterial || sourceMaterial->GetTextureCount(textureType) <= textureIndex)
				return 0;

			aiString importedTexturePath;
			if (sourceMaterial->GetTexture(textureType, textureIndex, &importedTexturePath) != AI_SUCCESS)
				return 0;

			std::string importedPathString = importedTexturePath.C_Str();
			std::replace(importedPathString.begin(), importedPathString.end(), '\\', static_cast<char>(std::filesystem::path::preferred_separator));
			std::filesystem::path texturePath = importedPathString;
			if (texturePath.empty())
				return 0;

			// Assimp uses *N for textures embedded in the source file. TextureAsset is
			// currently file-backed, so do not register an unloadable handle.
			if (texturePath.string().front() == '*')
			{
				NV_CORE_WARN("Embedded mesh texture {0} is not supported yet", texturePath.string());
				return 0;
			}

			const std::filesystem::path originalPath = texturePath;
			if (texturePath.is_relative())
				texturePath = (m_Properties.Filepath.parent_path() / texturePath).lexically_normal();

			if (!std::filesystem::exists(texturePath))
			{
				std::string filename = originalPath.filename().string();
				std::transform(filename.begin(), filename.end(), filename.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				const auto foundTexture = textureFilesByName.find(filename);
				if (foundTexture == textureFilesByName.end())
				{
					NV_CORE_WARN("Mesh texture does not exist: {0} (material reference: {1})",
						texturePath.string(), importedTexturePath.C_Str());
					return 0;
				}
				texturePath = foundTexture->second;
			}

			return AssetManager::ImportAsset(texturePath);
		};

		auto importSourceMaterialTextures = [&importMaterialTexture](const Ref<MaterialAsset>& materialAsset, aiMaterial* sourceMaterial) -> bool
		{
			if (!materialAsset || !sourceMaterial)
				return false;

			bool changed = false;
			if (materialAsset->GetAlbedoMapHandle() == 0)
			{
				const AssetHandle albedoMap = importMaterialTexture(sourceMaterial, aiTextureType_DIFFUSE);
				if (albedoMap != 0)
				{
					materialAsset->SetAlbedoMap(albedoMap);
					changed = true;
				}
			}

			if (materialAsset->GetNormalMapHandle() == 0)
			{
				AssetHandle normalMap = importMaterialTexture(sourceMaterial, aiTextureType_NORMALS);
				if (normalMap == 0)
					normalMap = importMaterialTexture(sourceMaterial, aiTextureType_HEIGHT);

				if (normalMap != 0)
				{
					materialAsset->SetNormalMap(normalMap);
					changed = true;
				}
			}

			// The bundled Assimp predates explicit PBR texture enums. FBX importers
			// commonly expose metallic/roughness as UNKNOWN (and sometimes put base
			// color there too), so classify every referenced texture by filename.
			for (int typeValue = static_cast<int>(aiTextureType_DIFFUSE);
				typeValue <= static_cast<int>(aiTextureType_UNKNOWN); ++typeValue)
			{
				const aiTextureType type = static_cast<aiTextureType>(typeValue);
				for (uint32_t textureIndex = 0; textureIndex < sourceMaterial->GetTextureCount(type); ++textureIndex)
				{
					aiString sourcePath;
					if (sourceMaterial->GetTexture(type, textureIndex, &sourcePath) != AI_SUCCESS)
						continue;

					std::string normalizedSourcePath = sourcePath.C_Str();
					std::replace(normalizedSourcePath.begin(), normalizedSourcePath.end(), '\\', '/');
					std::string name = std::filesystem::path(normalizedSourcePath).filename().string();
					std::transform(name.begin(), name.end(), name.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const auto contains = [&name](const char* token) { return name.find(token) != std::string::npos; };
					const AssetHandle texture = importMaterialTexture(sourceMaterial, type, textureIndex);
					if (!texture)
						continue;

					if (materialAsset->GetAlbedoMapHandle() == 0 &&
						(contains("basecolor") || contains("base_color") || contains("albedo") || contains("diffuse")))
					{
						materialAsset->SetAlbedoMap(texture);
						changed = true;
					}
					else if (materialAsset->GetNormalMapHandle() == 0 &&
						(contains("normal") || contains("_nrm") || contains("_nor")))
					{
						materialAsset->SetNormalMap(texture);
						changed = true;
					}
					else if (materialAsset->GetMetalnessMapHandle() == 0 &&
						(contains("metalness") || contains("metallic") || contains("_metal")))
					{
						materialAsset->SetMetalnessMap(texture);
						changed = true;
					}
					else if (materialAsset->GetRoughnessMapHandle() == 0 &&
						(contains("roughness") || contains("_rough")))
					{
						materialAsset->SetRoughnessMap(texture);
						changed = true;
					}
				}
			}

			return changed;
		};

		auto getMaterialAssetPath = [this](uint32_t materialIndex, aiMaterial* sourceMaterial) -> std::pair<std::string, std::filesystem::path>
		{
			const std::string meshSourceName = m_Properties.Filepath.stem().string();

			std::string aiMaterialName = sourceMaterial ? sourceMaterial->GetName().C_Str() : "";
			aiMaterialName = aiMaterialName.empty() ? std::to_string(materialIndex) : aiMaterialName;

			const std::string materialName = meshSourceName + "_" + aiMaterialName;
			const std::filesystem::path materialPath = AssetManager::GetAssetPath() / "Materials/Defaults" / meshSourceName / (materialName + ".cmaterial");
			return { materialName, materialPath };
		};

		auto loadExistingMaterialAsset = [&getMaterialAssetPath, &importSourceMaterialTextures](uint32_t materialIndex, aiMaterial* sourceMaterial) -> Ref<MaterialAsset>
		{
			const std::filesystem::path materialPath = getMaterialAssetPath(materialIndex, sourceMaterial).second;
			AssetHandle materialHandle = AssetManager::GetAssetHandleFromFilePath(materialPath);

			// The deterministic material file may predate its registry entry. Import it
			// instead of calling CreateNewAsset, which would generate "(01)", "(02)", ...
			if (!materialHandle && std::filesystem::exists(materialPath))
				materialHandle = AssetManager::ImportAsset(materialPath);

			if (!materialHandle)
				return nullptr;

			Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
			if (!materialAsset)
			{
				NV_CORE_WARN("Failed to load existing material asset {0}; a replacement will be created", materialPath.string());
				return nullptr;
			}

			if (importSourceMaterialTextures(materialAsset, sourceMaterial))
				AssetImporter::Serialize(materialAsset);

			NV_MESH_LOG("  Reused material asset {0} (Slot = {1})", materialPath.string(), materialIndex);
			return materialAsset;
		};

		auto createMaterialAsset = [this, &getMaterialAssetPath, &importSourceMaterialTextures](uint32_t materialIndex, aiMaterial* sourceMaterial) -> Ref<MaterialAsset>
		{
			const auto [materialName, materialPath] = getMaterialAssetPath(materialIndex, sourceMaterial);
			const std::string filename = materialPath.filename().string();
			const std::string materialDirectory = materialPath.parent_path().string();

			Ref<MaterialAsset> materialAsset = AssetManager::CreateNewAsset<MaterialAsset>(
				filename,
				materialDirectory,
				materialName);

			glm::vec3 albedoColor(0.8f);
			if (sourceMaterial)
			{
				aiColor3D aiColor;
				if (sourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiColor) == AI_SUCCESS)
					albedoColor = { aiColor.r, aiColor.g, aiColor.b };
			}

			float shininess = 80.0f;
			if (sourceMaterial)
				sourceMaterial->Get(AI_MATKEY_SHININESS, shininess);

			float roughness = 1.0f - glm::sqrt(shininess / 100.0f);
			roughness = glm::clamp(roughness, 0.0f, 1.0f);

			float metalness = 0.0f;
			if (sourceMaterial)
				sourceMaterial->Get(AI_MATKEY_REFLECTIVITY, metalness);

			materialAsset->SetAlbedoColor(albedoColor);
			materialAsset->SetUseNormalMap(false);
			materialAsset->SetMetalness(metalness);
			materialAsset->SetRoughness(roughness);

			importSourceMaterialTextures(materialAsset, sourceMaterial);

			AssetImporter::Serialize(materialAsset);

			NV_MESH_LOG("  Created material asset {0} (Slot = {1})", filename, materialIndex);
			return materialAsset;
		};

		const uint32_t materialSlotCount = getMaterialSlotCount();
		m_MaterialsAsset.clear();
		m_MaterialsAsset.resize(materialSlotCount);

		for (const auto& [materialSlot, materialHandle] : m_Properties.MaterialSlots)
		{
			if (materialSlot >= m_MaterialsAsset.size())
			{
				NV_CORE_WARN("Mesh material table references invalid material slot {0}", materialSlot);
				continue;
			}

			Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
			if (materialAsset)
				m_MaterialsAsset[materialSlot] = materialAsset;
			else
				NV_CORE_WARN("Invalid material handle for material slot {0}; using imported/default material", materialSlot);
		}

		if (!m_Properties.SubmeshMaterials.empty())
		{
			for (const auto& [submeshIndex, materialHandle] : m_Properties.SubmeshMaterials)
			{
				if (submeshIndex >= m_Submeshes.size())
				{
					NV_CORE_WARN("Mesh material override references invalid submesh index {0}", submeshIndex);
					continue;
				}

				const uint32_t materialIndex = m_Submeshes[submeshIndex].MaterialIndex;
				if (materialIndex >= m_MaterialsAsset.size())
					m_MaterialsAsset.resize(materialIndex + 1);
				if (m_MaterialsAsset[materialIndex])
					continue;

				Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				if (materialAsset)
					m_MaterialsAsset[materialIndex] = materialAsset;
				else
					NV_CORE_WARN("Invalid material override handle for submesh {0}; using imported/default material", submeshIndex);
			}
		}

		for (uint32_t materialIndex = 0; materialIndex < m_MaterialsAsset.size(); materialIndex++)
		{
			aiMaterial* aiMaterial = nullptr;
			if (m_Scene && m_Scene->HasMaterials() && materialIndex < m_Scene->mNumMaterials)
				aiMaterial = m_Scene->mMaterials[materialIndex];

			if (m_MaterialsAsset[materialIndex])
			{
				if (importSourceMaterialTextures(m_MaterialsAsset[materialIndex], aiMaterial))
					AssetImporter::Serialize(m_MaterialsAsset[materialIndex]);
				continue;
			}

			m_MaterialsAsset[materialIndex] = loadExistingMaterialAsset(materialIndex, aiMaterial);
			if (!m_MaterialsAsset[materialIndex])
				m_MaterialsAsset[materialIndex] = createMaterialAsset(materialIndex, aiMaterial);
		}

		for (Submesh& submesh : m_Submeshes)
		{
			if (submesh.MaterialIndex >= m_MaterialsAsset.size() || !m_MaterialsAsset[submesh.MaterialIndex])
			{
				NV_CORE_WARN("Submesh {0} has invalid material index {1}; falling back to slot 0", submesh.MeshName, submesh.MaterialIndex);
				submesh.MaterialIndex = 0;
			}
		}
	}

	////////////////////////////////////////////////////////
	// Mesh
	////////////////////////////////////////////////////////
	Mesh::Mesh(Ref<MeshSource> meshSource)
		: m_MeshSource(meshSource)
	{
		const auto& meshMaterials = meshSource->GetMaterialsAsset();

		uint32_t materialSlotCount = (uint32_t)meshMaterials.size();
		for (const Submesh& submesh : meshSource->GetSubmeshes())
			materialSlotCount = std::max(materialSlotCount, submesh.MaterialIndex + 1);

		materialSlotCount = std::max(materialSlotCount, 1u);
		m_Materials = CreateRef<MaterialTable>(materialSlotCount);

		for (uint32_t i = 0; i < meshMaterials.size(); i++)
		{
			if (!meshMaterials[i])
				continue;

			m_Materials->SetMaterial(i, meshMaterials[i]->Handle);
		}
	}

	Mesh::Mesh(const Ref<MeshSource>& meshSource, const Ref<MaterialTable>& materialTable)
		: m_MeshSource(meshSource), m_Materials(materialTable)
	{
	}

	Mesh::Mesh(const Ref<Mesh>& other)
		: m_MeshSource(other->m_MeshSource), m_Materials(other->m_Materials)
	{}

	Mesh::~Mesh()
	{
	}
}

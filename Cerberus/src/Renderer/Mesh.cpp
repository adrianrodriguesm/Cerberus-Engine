#include "pch.h"
#include "Mesh.h"
#include "Renderer/RendererResources.h"
#include "Asset/AssetManager.h"
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <filesystem>
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

		m_Importer = std::make_unique<Assimp::Importer>();
		const aiScene* scene = m_Importer->ReadFile(m_Properties.Filepath.string(), s_MeshImportFlags);

		if (!scene || !scene->HasMeshes())
		{
			NV_CORE_ERROR("Failed to load mesh file: {0}", m_Properties.Filepath);
			return;
		}

		m_Scene = scene;
		m_MeshShader = RendererResources::GetShaderLibrary().Get("PBR");

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
				Vertex vertex;
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

		//m_VertexBuffer = CreateRef<VertexBuffer>(m_Vertices.data(), (uint32_t)(m_Vertices.size() * sizeof(Vertex)));
		m_VertexBufferLayout = {
			{ ShaderDataType::Float3, "a_Position"	},
			{ ShaderDataType::Float3, "a_Normal"	},
			{ ShaderDataType::Float3, "a_Tangent"	},
			{ ShaderDataType::Float3, "a_Binormal"	},
			{ ShaderDataType::Float2, "a_TexCoord"	},
		};
		//m_VertexBuffer->SetVertexBufferLayout(m_VertexBufferLayout);
		//m_IndexBuffer = CreateRef<IndexBuffer>(m_Indices.data(), (uint32_t)(m_Indices.size() * sizeof(Index)));

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
#if OLD_RENDERER
		if (!m_Properties.SubmeshMaterials.empty())
		{
			for (uint32_t submeshIndex = 0; submeshIndex < m_Properties.SubmeshMaterials.size(); submeshIndex++)
			{
				auto& materialHandle = m_Properties.SubmeshMaterials[submeshIndex];
				uint32_t materialIndex = m_Submeshes[submeshIndex].MaterialIndex;
				Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				if (materialAsset)
				{
					if (materialIndex >= m_MaterialsAsset.size())
						m_MaterialsAsset.resize(materialIndex + 1);

					m_MaterialsAsset[materialIndex] = materialAsset;
				}
				else
				{
					if (materialIndex >= m_MaterialsAsset.size())
						m_MaterialsAsset.resize(materialIndex + 1);

					m_MaterialsAsset[materialIndex] = Renderer::GetDefaultPBRMaterial();
				}
			}

		}
		else
		{
			Handle<Texture2D> whiteTexture = Renderer::GetWhiteTexture();
			if (m_Scene->HasMaterials())
			{
				m_MaterialsAsset.resize(m_Scene->mNumMaterials);
				std::string meshSourceName = m_Properties.Filepath.stem().string();
				for (uint32_t i = 0; i < m_Scene->mNumMaterials; i++)
				{
					auto aiMaterial = m_Scene->mMaterials[i];
					std::string aiMaterialName = aiMaterial->GetName().C_Str();
					aiMaterialName = (aiMaterialName == "") ? std::to_string(i) : aiMaterialName;
					std::string filename = meshSourceName + "_" + aiMaterialName;
					Ref<Material> material = CreateRef<Material>(m_MeshShader, filename);
					// CreateMaterialAsset
					filename += ".cmaterial";
					std::string materialPath = "Materials/Defaults/" + meshSourceName;
					std::string filepath = (AssetManager::GetAssetPath() / materialPath).string();
					Ref<MaterialAsset> materialAsset = AssetManager::CreateNewAsset<MaterialAsset>(filename, filepath, material);
					m_MaterialsAsset[i] = materialAsset;

					NV_MESH_LOG("  {0} (Index = {1})", filename, i);
					aiString aiTexPath;
					uint32_t textureCount = aiMaterial->GetTextureCount(aiTextureType_DIFFUSE);
					NV_MESH_LOG("    TextureCount = {0}", textureCount);
					///////////////////////////
					/// ALBEDO
					///////////////////////////
					if (aiMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &aiTexPath) == AI_SUCCESS)
					{
						std::filesystem::path path = m_Properties.Filepath;
						auto parentPath = path.parent_path();
						parentPath /= std::string(aiTexPath.data);
						std::string texturePath = parentPath.string();
						NV_MESH_LOG("    Albedo map path = {0}", texturePath);
						TextureProperties textureProperties;
						textureProperties.SRGB = true;
						textureProperties.GenerateMips = true;
						AssetHandle albedoAssetHandle = AssetManager::GetMetadata(parentPath).Handle;
#if OLD_RENDERER
						Ref<Texture2D> texture = AssetManager::OverrideAsset<Texture2D>(albedoAssetHandle, texturePath, textureProperties);
						if (texture->Loaded())
						{
							materialAsset->SetAlbedoColor(glm::vec3(1.f));
							materialAsset->SetAlbedoMap(texture);
						}
						else
						{
							NV_CORE_ERROR("Could not load albedo texture: {0}", texturePath);
						}
#endif
					}
					else
					{
						glm::vec3 albedoColor(0.8f);
						aiColor3D aiColor;
						if (aiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiColor) == AI_SUCCESS)
							albedoColor = { aiColor.r, aiColor.g, aiColor.b };

						materialAsset->SetAlbedoColor(albedoColor);
						materialAsset->SetAlbedoMap(whiteTexture);
					}
					///////////////////////////
					/// NORMALS
					///////////////////////////
					if (aiMaterial->GetTexture(aiTextureType_NORMALS, 0, &aiTexPath) == AI_SUCCESS)
					{
						std::filesystem::path path = m_Properties.Filepath;
						auto parentPath = path.parent_path();
						parentPath /= std::string(aiTexPath.data);
						std::string texturePath = parentPath.string();
						NV_MESH_LOG("    Normal map path = {0}", texturePath);
						TextureProperties textureProperties;
						textureProperties.SRGB = false;
						textureProperties.GenerateMips = true;
						AssetHandle albedoAssetHandle = AssetManager::GetMetadata(parentPath).Handle;
#if OLD_RENDERER
						Ref<Texture2D> texture = AssetManager::OverrideAsset<Texture2D>(albedoAssetHandle, texturePath, textureProperties);
						if (texture->Loaded())
						{

							materialAsset->SetNormalMap(texture);
							materialAsset->SetUseNormalMap(true);
						}
						else
						{
							NV_CORE_ERROR("Could not load normal texture: {0}", texturePath);
						}
#endif
					}
					else
					{
						materialAsset->SetNormalMap(whiteTexture);
						materialAsset->SetUseNormalMap(false);
					}

					///////////////////////////
					/// ROUGHNESS
					///////////////////////////
					if (aiMaterial->GetTexture(aiTextureType_SHININESS, 0, &aiTexPath) == AI_SUCCESS)
					{
						std::filesystem::path path = m_Properties.Filepath;
						auto parentPath = path.parent_path();
						parentPath /= std::string(aiTexPath.data);
						std::string texturePath = parentPath.string();

						NV_MESH_LOG("    Roughness map path = {0}", texturePath);
						TextureProperties textureProperties;
						textureProperties.SRGB = false;
						textureProperties.GenerateMips = true;
						AssetHandle albedoAssetHandle = AssetManager::GetMetadata(parentPath).Handle;
#if OLD_RENDERER
						Ref<Texture2D> texture = AssetManager::OverrideAsset<Texture2D>(albedoAssetHandle, texturePath, textureProperties);
						if (texture->Loaded())
						{
							materialAsset->SetRoughness(1.f);
							materialAsset->SetRoughnessMap(texture);
						}
						else
						{
							NV_CORE_ERROR("Could not load Roughness texture: {0}", texturePath);
						}
#endif
					}
					else
					{
						float shininess;
						if (aiMaterial->Get(AI_MATKEY_SHININESS, shininess) != aiReturn_SUCCESS)
							shininess = 80.0f; // Default value

						float roughness = 1.0f - glm::sqrt(shininess / 100.0f);

						materialAsset->SetRoughness(roughness);
						materialAsset->SetRoughnessMap(whiteTexture);
					}
					///////////////////////////
					/// METALNESS
					///////////////////////////
					bool metalnessTextureFound = false;
					for (uint32_t i = 0; i < aiMaterial->mNumProperties; i++)
					{
						auto prop = aiMaterial->mProperties[i];
						if (prop->mType == aiPTI_String)
						{
							uint32_t strLength = *(uint32_t*)prop->mData;
							std::string str(prop->mData + 4, strLength);

							std::string key = prop->mKey.data;
							if (key == "$raw.ReflectionFactor|file")
							{
								metalnessTextureFound = true;
								std::filesystem::path path = m_Properties.Filepath;
								auto parentPath = path.parent_path();
								parentPath /= str;
								std::string texturePath = parentPath.string();
								NV_MESH_LOG("    Metalness map path = {0}", texturePath);
								TextureProperties textureProperties;
								textureProperties.SRGB = false;
								textureProperties.GenerateMips = true;
								AssetHandle albedoAssetHandle = AssetManager::GetMetadata(parentPath).Handle;
#if OLD_RENDERER
								Ref<Texture2D> texture = AssetManager::OverrideAsset<Texture2D>(albedoAssetHandle, texturePath, textureProperties);
								if (texture->Loaded())
								{
									materialAsset->SetMetalness(1.f);
									materialAsset->SetMetalnessMap(texture);
								}
								else
								{
									NV_CORE_ERROR("Could not load Metallic texture: {0}", texturePath);
								}
#endif
								break;
							}
						}
					}
					if (!metalnessTextureFound)
					{
						NV_MESH_LOG("	No Metalness map associated");

						float metalness;
						if (aiMaterial->Get(AI_MATKEY_REFLECTIVITY, metalness) != aiReturn_SUCCESS)
							metalness = 0.0f;

						materialAsset->SetMetalness(metalness);
						materialAsset->SetMetalnessMap(whiteTexture);
					}

					AssetImporter::Serialize(materialAsset);

				}
			}
		}
#endif

	}

	////////////////////////////////////////////////////////
	// Mesh
	////////////////////////////////////////////////////////
	Mesh::Mesh(Ref<MeshSource> meshSource)
		: m_MeshSource(meshSource)
	{
		const auto& meshMaterials = meshSource->GetMaterialsAsset();
		m_Materials = CreateRef<MaterialTable>(meshMaterials.size());

		for (uint32_t i = 0; i < meshMaterials.size(); i++)
			m_Materials->SetMaterial(i, meshMaterials[i]->Handle);
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
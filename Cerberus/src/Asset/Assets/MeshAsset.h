#pragma once
#include "Renderer/RHI/Buffers/IndexBuffer.h"
#include "Renderer/RHI/Buffers/VertexBuffer.h"
#include "Asset/Assets/MaterialAsset.h"
#include "Asset/Asset.h"
#include <glm/glm.hpp>

struct aiNode;
struct aiAnimation;
struct aiNodeAnim;
struct aiScene;

namespace Assimp {
	class Importer;
}

namespace cerberus
{
	using MeshSourceHandle = AssetHandle;
	using MeshHandle = AssetHandle;

	class Submesh
	{
	public:
		uint32_t BaseVertex;
		uint32_t BaseIndex;
		uint32_t MaterialIndex;
		uint32_t IndexCount;
		uint32_t VertexCount;

		glm::mat4 Transform{ 1.0f };

		std::string NodeName, MeshName;
	};

	struct MeshSourceProperties
	{
		std::filesystem::path Filepath;
		// Version 2: imported/serialized materials keyed by Assimp material slot.
		std::map<uint32_t, AssetHandle> MaterialSlots;

		// Version 1 compatibility: old .meta files keyed materials by submesh.
		// They are migrated to MaterialSlots the next time the MeshSource is loaded.
		std::map<uint32_t, AssetHandle> SubmeshMaterials;
	};

	// MeshSource is a representation of an actual asset file on disk (e.g .fbx, .obj, etc)
	// Meshes are basically intances created from a MeshSource
	class MeshSource : public Asset
	{
	public:
		struct Vertex
		{
			glm::vec3 Position;
			glm::vec3 Normal;
			glm::vec3 Tangent;
			glm::vec3 Binormal;
			glm::vec2 Texcoord;
		};
		struct Index
		{
			uint32_t Index0, Index1, Index2;
		};
	public:
		MeshSource(const MeshSourceProperties& properties);
		~MeshSource();

		std::vector<Submesh>& GetSubmeshes() { return m_Submeshes; }
		const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }

		const std::vector<Index>& GetIndices() const { return m_Indices; }
		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }

		std::vector<Ref<MaterialAsset>>& GetMaterialsAsset() { return m_MaterialsAsset; }
		const std::vector<Ref<MaterialAsset>>& GetMaterialsAsset() const { return m_MaterialsAsset; }
		uint32_t GetMaterialSlotCount() const { return (uint32_t)m_MaterialsAsset.size(); }

		static AssetType GetStaticType() { return AssetType::MeshSource; }
		AssetType GetAssetType() const { return GetStaticType(); }


	private:
		void LoadGeometry();
		void LoadMaterials();
		void TraverseNodes(aiNode* node, const glm::mat4& parentTransform = glm::mat4(1.0f), uint32_t level = 0);
	private:
		MeshSourceProperties m_Properties;
		Scope<Assimp::Importer> m_Importer;

		std::vector<Index> m_Indices;
		std::vector<Vertex> m_Vertices;
		std::vector<Submesh> m_Submeshes;

		// Imported material slots. Submesh::MaterialIndex indexes this array.
		std::vector<Ref<MaterialAsset>> m_MaterialsAsset;

		// Assimp
		const aiScene* m_Scene;

		friend class MeshSourceSerializer;
	};

	class Mesh : public Asset
	{
	public:
		explicit Mesh(Ref<MeshSource> meshSource);
		Mesh(const Ref<MeshSource>& meshSource, const Ref<MaterialTable>& materialTable);
		Mesh(const Ref<Mesh>& other);
		virtual ~Mesh();

		Ref<MeshSource> GetMeshSource() { return m_MeshSource; }
		Ref<MeshSource> GetMeshSource() const { return m_MeshSource; }
		void SetMeshAsset(Ref<MeshSource>& meshSource) { m_MeshSource = meshSource; }

		Ref<MaterialTable> GetMaterialTable() const { return m_Materials; }

		static AssetType GetStaticType() { return AssetType::Mesh; }
		AssetType GetAssetType() const { return GetStaticType(); }

	private:
		Ref<MeshSource> m_MeshSource;

		// Materials
		Ref<MaterialTable> m_Materials;
	};
}

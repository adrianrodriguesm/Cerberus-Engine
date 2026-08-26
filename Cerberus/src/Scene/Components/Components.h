#pragma once
#include "Math/Matrix.h"
#include "Asset/Asset.h"
#include "Asset/Asset.h"
#include "Asset/AssetManager.h"
#include "Scene/EnvironmentMap.h"
#include "Asset/Assets/MeshAsset.h"
namespace cerberus
{
	class Scene;
	/////////////////////////////////////////////////////////////////////// 
	/// IDComponent
	///////////////////////////////////////////////////////////////////////
	struct IDComponent
	{
		UUID ID = 0;
	};
	///////////////////////////////////////////////////////////////////////
	/// RelationshipComponent
	///////////////////////////////////////////////////////////////////////
	// Parent/child links, referenced by UUID (not entt::entity) so they
	// survive serialization and remain valid across registry relocation.
	struct RelationshipComponent
	{
		UUID Parent = 0;
		std::vector<UUID> Children;
	};
	///////////////////////////////////////////////////////////////////////
	/// TagComponent
	///////////////////////////////////////////////////////////////////////
	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const std::string& tag)
			: Tag(tag) {}
		TagComponent(const TagComponent&) = default;

		operator std::string& () { return Tag; }
		operator const std::string& () const { return Tag; }
	};
	/////////////////////////////////////////////////////////////////////// 
	/// TransformComponent
	///////////////////////////////////////////////////////////////////////
	// TODO: this needs a huge refactor maybe create a scene component thats hold a rendertransform and leave this transform for the entity (just like unreal does)
	class TransformComponent
	{
	public:
		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;
		TransformComponent(const glm::vec3& translation)
			: m_Translation(translation) {}

		glm::mat4 GetTransform() const
		{
			return math::MatFactory::CreateTransformMatrix(m_Translation, glm::radians(m_Rotation), m_Scale);
		}

		void SetRotation(const glm::vec3& rotation)
		{
			if (rotation != m_Rotation)
			{
				m_Rotation = rotation;
				MarkTransformAsDirty();
			}
		}

		void SetTranslation(const glm::vec3& translation)
		{
			if (translation != m_Translation)
			{
				m_Translation = translation;
				MarkTransformAsDirty();
			}
		}

		void SetScale(const glm::vec3& scale)
		{
			if (m_Scale != scale)
			{
				m_Scale = scale;
				MarkTransformAsDirty();
			}
		}

		glm::vec3 GetTranslation() { return m_Translation; }
		glm::vec3 GetRotation() { return m_Rotation; }
		glm::vec3 GetScale() { return m_Scale; }


		void Reset()
		{
			m_Translation = { 0.0f, 0.0f, 0.0f };
			m_Rotation = { 0.0f, 0.0f, 0.0f };
			m_Scale = { 1.0f, 1.0f, 1.0f };
			MarkTransformAsDirty();
		}

		void MarkTransformAsDirty() { m_TransformStateDirty = true; }

		bool IsTransformDirty() { return m_TransformStateDirty; }
	private:
		/** Is this component in need of its whole state being sent to the renderer? */
		uint8_t m_TransformStateDirty : 1 = true;
		/** The actual transform data */
		glm::vec3 m_Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Scale = { 1.0f, 1.0f, 1.0f };

		friend class Scene;
	};

	/////////////////////////////////////////////////////////////////////// 
	/// MeshComponent
	///////////////////////////////////////////////////////////////////////
	struct MeshComponent
	{
		AssetHandle MeshHandle = 0;
		Ref<MaterialTable> MaterialTable;
		// -1 registers every submesh (today's default behavior). >= 0 filters
		// registration down to that single submesh index, for entities that
		// represent one submesh of a split multi-submesh mesh.
		int32_t SubmeshIndex = -1;

		MeshComponent()
			: MaterialTable(CreateRef<cerberus::MaterialTable>()),
			  m_Registered(false)
		{
		}

		MeshComponent(const MeshComponent& other)
			: MeshHandle(other.MeshHandle),
			  MaterialTable(other.MaterialTable
				  ? CreateRef<cerberus::MaterialTable>(other.MaterialTable)
				  : CreateRef<cerberus::MaterialTable>()),
			  SubmeshIndex(other.SubmeshIndex),
			  m_Registered(false)
		{
		}

		// Preserve renderer state when EnTT relocates component storage. Explicit
		// copies represent a new logical component and intentionally reset it.
		MeshComponent(MeshComponent&& other) noexcept = default;
		MeshComponent& operator=(MeshComponent&& other) noexcept = default;

		MeshComponent& operator=(const MeshComponent& other)
		{
			if (this == &other)
				return *this;

			MeshHandle = other.MeshHandle;
			MaterialTable = other.MaterialTable
				? CreateRef<cerberus::MaterialTable>(other.MaterialTable)
				: CreateRef<cerberus::MaterialTable>();
			SubmeshIndex = other.SubmeshIndex;
			m_Registered = false;
			return *this;
		}

		MeshComponent(const AssetHandle& meshHandle)
			: MeshHandle(meshHandle), m_Registered(false)
		{
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(MeshHandle);
			if (mesh)
				MaterialTable = CreateRef<cerberus::MaterialTable>(mesh->GetMaterialTable());
			else
				MaterialTable = CreateRef<cerberus::MaterialTable>();
		}

	protected:
		/** Indicates whether this logical object has a proxy in the active RenderScene. */
		uint8_t m_Registered : 1;

		friend class Scene;
	};
	/////////////////////////////////////////////////////////////////////// 
	/// DirectionalLightComponent
	///////////////////////////////////////////////////////////////////////
	struct DirectionalLightComponent
	{
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;

		DirectionalLightComponent() = default;
		DirectionalLightComponent(const DirectionalLightComponent& other) = default;
		DirectionalLightComponent(const glm::vec3& radiance, const float intesity)
			:Radiance(radiance), Intensity(intesity)
		{}
	};
	/////////////////////////////////////////////////////////////////////// 
	/// SkyLightComponent
	///////////////////////////////////////////////////////////////////////
	struct SkyLightComponent
	{
		AssetHandle EnvironmentHandle;
		float Intensity = 1.f;
		uint32_t SampleLOD = 1;

		SkyLightComponent() = default;
		SkyLightComponent(const SkyLightComponent& other) = default;
		SkyLightComponent(const AssetHandle& environment, const float intesity, const uint32_t lod)
			: EnvironmentHandle(environment), Intensity(intesity), SampleLOD(lod)
		{}
	};

}

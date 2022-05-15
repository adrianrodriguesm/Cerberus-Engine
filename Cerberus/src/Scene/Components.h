#pragma once
#include "Math/Matrix.h"
#include "Renderer/Mesh.h"
#include "Asset/Asset.h"
#include "Asset/AssetManager.h"
#include "Scene/EnvironmentMap.h"
namespace cerberus
{
	/////////////////////////////////////////////////////////////////////// 
	/// IDComponent
	///////////////////////////////////////////////////////////////////////
	struct IDComponent
	{
		UUID ID = 0;
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
	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;
		TransformComponent(const glm::vec3& translation)
			: Translation(translation) {}

		glm::mat4 GetTransform() const
		{
			return math::MatFactory::CreateTransformMatrix(Translation, glm::radians(Rotation), Scale);
		}

		void Reset()
		{
			Translation = { 0.0f, 0.0f, 0.0f };
			Rotation = { 0.0f, 0.0f, 0.0f };
			Scale = { 1.0f, 1.0f, 1.0f };
		}
	};
	/////////////////////////////////////////////////////////////////////// 
	/// MeshComponent
	///////////////////////////////////////////////////////////////////////
	struct MeshComponent
	{
		AssetHandle MeshHandle = 0;
		Ref<MaterialTable> MaterialTable;

		MeshComponent() = default;
		MeshComponent(const MeshComponent& other) = default;

		MeshComponent(const AssetHandle& meshHandle)
			: MeshHandle(meshHandle)
		{
			Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(MeshHandle);
			if(mesh)
				MaterialTable = CreateRef<cerberus::MaterialTable>(mesh->GetMaterialTable());
			else
				MaterialTable = CreateRef<cerberus::MaterialTable>();
		}
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
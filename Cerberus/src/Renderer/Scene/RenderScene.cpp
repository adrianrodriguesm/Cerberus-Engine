#include "pch.h"
#include "RenderScene.h"

#include "Renderer/Memory/DeferredResourceDeletionQueue.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RendererResources.h"
#include "Renderer/RHI/Texture.h"
#include "Asset/Assets/TextureAsset.h"
#include "Asset/AssetManager.h"

#include <algorithm>
#include <cmath>

namespace cerberus
{
	RenderScene::~RenderScene()
	{
		Shutdown();
	}

	const MaterialProxy* RenderScene::GetMaterialProxy(uint32_t materialProxyIndex) const
	{
		if (materialProxyIndex == InvalidMaterialProxyIndex || materialProxyIndex >= m_MeshRegister.MaterialProxies.size())
			return nullptr;

		return &m_MeshRegister.MaterialProxies[materialProxyIndex];
	}

	void RenderScene::SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD)
	{
		const Ref<EnvironmentMap> resolvedEnvironment = environment
			? environment
			: RendererResources::GetEmptyEnvironmentMap();

		m_SkyLight.RadianceMap = resolvedEnvironment && resolvedEnvironment->RadianceMap.IsValid()
			? resolvedEnvironment->RadianceMap
			: RendererResources::GetBlackCubeTexture();
		m_SkyLight.IrradianceMap = resolvedEnvironment && resolvedEnvironment->IrradianceMap.IsValid()
			? resolvedEnvironment->IrradianceMap
			: RendererResources::GetBlackCubeTexture();
		m_SkyLight.Intensity = glm::max(intensity, 0.0f);

		TextureCube* radianceMap = ResourceManager::GetTextureCube(m_SkyLight.RadianceMap);
		const uint32_t maxLOD = radianceMap && radianceMap->GetMipLevelCount() > 0
			? radianceMap->GetMipLevelCount() - 1
			: 0;
		m_SkyLight.SampleLOD = static_cast<float>(glm::min(sampleLOD, maxLOD));
	}

	bool RenderScene::SetDirectionalLights(std::span<const DirectionalLight> directionalLights)
	{
		std::array<DirectionalLightProxy, MaxDirectionalLightCount> resolvedLights{};
		const uint32_t resolvedLightCount = static_cast<uint32_t>(std::min(
			directionalLights.size(),
			static_cast<size_t>(MaxDirectionalLightCount)));

		for (uint32_t lightIndex = 0; lightIndex < resolvedLightCount; ++lightIndex)
		{
			const DirectionalLight& sourceLight = directionalLights[lightIndex];
			DirectionalLightProxy& resolvedLight = resolvedLights[lightIndex];

			const float directionLengthSquared = glm::dot(sourceLight.Direction, sourceLight.Direction);
			resolvedLight.Direction = directionLengthSquared > 0.000001f
				? sourceLight.Direction * glm::inversesqrt(directionLengthSquared)
				: glm::vec3(0.0f, 1.0f, 0.0f);
			resolvedLight.Radiance = glm::max(sourceLight.Radiance, glm::vec3(0.0f));
			resolvedLight.Intensity = glm::max(sourceLight.Multiplier, 0.0f);
		}

		bool changed = m_DirectionalLightCount != resolvedLightCount;
		constexpr float epsilon = 0.000001f;
		for (uint32_t lightIndex = 0; !changed && lightIndex < MaxDirectionalLightCount; ++lightIndex)
		{
			const DirectionalLightProxy& previousLight = m_DirectionalLights[lightIndex];
			const DirectionalLightProxy& resolvedLight = resolvedLights[lightIndex];
			changed =
				glm::any(glm::greaterThan(glm::abs(previousLight.Direction - resolvedLight.Direction), glm::vec3(epsilon))) ||
				glm::any(glm::greaterThan(glm::abs(previousLight.Radiance - resolvedLight.Radiance), glm::vec3(epsilon))) ||
				std::abs(previousLight.Intensity - resolvedLight.Intensity) > epsilon;
		}

		m_DirectionalLights = resolvedLights;
		m_DirectionalLightCount = resolvedLightCount;
		return changed;
	}

	Ref<MaterialAsset> RenderScene::ResolveMaterialAsset(
		const Ref<MeshSource>& meshSource,
		const Ref<MaterialTable>& materialTable,
		uint32_t materialSlot) const
	{
		if (materialTable && materialTable->HasMaterial(materialSlot))
		{
			const AssetHandle materialHandle = materialTable->GetMaterial(materialSlot);
			if (materialHandle != 0)
			{
				Ref<MaterialAsset> overrideMaterial = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				if (overrideMaterial)
					return overrideMaterial;

				NV_CORE_WARN("Mesh material override for slot {0} is invalid; using source/default material", materialSlot);
			}
		}

		if (meshSource)
		{
			const auto& sourceMaterials = meshSource->GetMaterialsAsset();
			if (materialSlot < sourceMaterials.size() && sourceMaterials[materialSlot])
				return sourceMaterials[materialSlot];
		}

		return RendererResources::GetDefaultPBRMaterial();
	}

	uint32_t RenderScene::RegisterMaterialProxy(const Ref<MaterialAsset>& materialAsset)
	{
		Ref<MaterialAsset> resolvedMaterial = materialAsset ? materialAsset : RendererResources::GetDefaultPBRMaterial();
		const Ref<MaterialAsset> defaultMaterial = RendererResources::GetDefaultPBRMaterial();

		// Renderer-owned default resources have no AssetHandle. Keep a dedicated entry
		// so every fallback submesh gets the same stable proxy index.
		if (resolvedMaterial == defaultMaterial || resolvedMaterial->Handle == 0)
		{
			if (m_MeshRegister.DefaultMaterialProxyIndex == InvalidMaterialProxyIndex)
			{
				const uint32_t materialProxyIndex = static_cast<uint32_t>(m_MeshRegister.MaterialProxies.size());

				MaterialProxy proxy{};
				proxy.SourceMaterialAsset = 0;
				proxy.GPUMaterialIndex = materialProxyIndex;
				proxy.GPUData = BuildMaterialGPUData(resolvedMaterial);

				m_MeshRegister.DefaultMaterialProxyIndex = materialProxyIndex;
				m_MeshRegister.MaterialProxies.emplace_back(proxy);
			}

			return m_MeshRegister.DefaultMaterialProxyIndex;
		}

		const AssetHandle materialHandle = resolvedMaterial->Handle;
		if (auto it = m_MeshRegister.MaterialRegistered.find(materialHandle); it != m_MeshRegister.MaterialRegistered.end())
			return it->second;

		const uint32_t materialProxyIndex = static_cast<uint32_t>(m_MeshRegister.MaterialProxies.size());

		MaterialProxy proxy{};
		proxy.SourceMaterialAsset = materialHandle;
		proxy.GPUMaterialIndex = materialProxyIndex;
		proxy.GPUData = BuildMaterialGPUData(resolvedMaterial);

		m_MeshRegister.MaterialProxies.emplace_back(proxy);
		m_MeshRegister.MaterialRegistered[materialHandle] = materialProxyIndex;

		return materialProxyIndex;
	}

	uint32_t RenderScene::RegisterTextureProxy(AssetHandle textureAssetHandle, bool srgb)
	{
		if (textureAssetHandle == 0)
			return InvalidMaterialTextureIndex;

		auto& registeredTextures = srgb
			? m_MeshRegister.SRGBTextureRegistered
			: m_MeshRegister.LinearTextureRegistered;

		if (auto it = registeredTextures.find(textureAssetHandle); it != registeredTextures.end())
			return it->second;

		Ref<TextureAsset> textureAsset = AssetManager::GetAsset<TextureAsset>(textureAssetHandle);
		if (!textureAsset || !textureAsset->Loaded())
		{
			NV_CORE_WARN("Material texture asset {0} could not be loaded", (uint64_t)textureAssetHandle);
			return InvalidMaterialTextureIndex;
		}

		CORE_ASSERT(m_MeshRegister.GPUTextures.size() < MaxMaterialTextureCount, "Material texture table capacity exceeded");

		Handle<Texture2D> gpuTexture = ResourceManager::CreateTexture2D(
			textureAsset,
			srgb ? TextureColorSpace::SRGB : TextureColorSpace::Linear);
		CORE_ASSERT(gpuTexture.IsValid(), "Failed to create GPU texture for the material table");

		const uint32_t textureIndex = static_cast<uint32_t>(m_MeshRegister.GPUTextures.size());
		m_MeshRegister.GPUTextures.push_back(gpuTexture);
		registeredTextures[textureAssetHandle] = textureIndex;
		return textureIndex;
	}

	GPUMaterialData RenderScene::BuildMaterialGPUData(const Ref<MaterialAsset>& materialAsset)
	{
		CORE_ASSERT(materialAsset, "Cannot build GPU data for a null MaterialAsset");

		const uint32_t albedoTextureIndex = RegisterTextureProxy(materialAsset->GetAlbedoMapHandle(), true);
		const uint32_t normalTextureIndex = RegisterTextureProxy(materialAsset->GetNormalMapHandle(), false);
		const uint32_t metalnessTextureIndex = RegisterTextureProxy(materialAsset->GetMetalnessMapHandle(), false);
		const uint32_t roughnessTextureIndex = RegisterTextureProxy(materialAsset->GetRoughnessMapHandle(), false);

		return materialAsset->BuildGPUMaterialData(
			albedoTextureIndex,
			normalTextureIndex,
			metalnessTextureIndex,
			roughnessTextureIndex);
	}

	bool RenderScene::UpdateMaterialProxy(const Ref<MaterialAsset>& materialAsset)
	{
		if (!materialAsset)
			return false;

		uint32_t materialProxyIndex = InvalidMaterialProxyIndex;
		const Ref<MaterialAsset> defaultMaterial = RendererResources::GetDefaultPBRMaterial();
		if (materialAsset == defaultMaterial || materialAsset->Handle == 0)
		{
			materialProxyIndex = m_MeshRegister.DefaultMaterialProxyIndex;
		}
		else
		{
			auto materialIt = m_MeshRegister.MaterialRegistered.find(materialAsset->Handle);
			if (materialIt == m_MeshRegister.MaterialRegistered.end())
				return false;

			materialProxyIndex = materialIt->second;
		}

		if (materialProxyIndex == InvalidMaterialProxyIndex || materialProxyIndex >= m_MeshRegister.MaterialProxies.size())
			return false;

		m_MeshRegister.MaterialProxies[materialProxyIndex].GPUData = BuildMaterialGPUData(materialAsset);
		return true;
	}

	void RenderScene::ClearMaterialProxies()
	{
		ClearTextureProxies();
		m_MeshRegister.MaterialRegistered.clear();
		m_MeshRegister.MaterialProxies.clear();
		m_MeshRegister.DefaultMaterialProxyIndex = InvalidMaterialProxyIndex;
	}

	void RenderScene::ClearTextureProxies()
	{
		for (Handle<Texture2D> texture : m_MeshRegister.GPUTextures)
		{
			if (texture.IsValid())
				ResourceManager::DestroyTexture2D(texture);
		}

		m_MeshRegister.GPUTextures.clear();
		m_MeshRegister.SRGBTextureRegistered.clear();
		m_MeshRegister.LinearTextureRegistered.clear();
	}

	bool RenderScene::RegisterMesh(
		RenderObjectID renderObjectID,
		const Ref<Mesh>& mesh,
		const Ref<MaterialTable>& materialTable,
		const glm::mat4& transform)
	{
		if (!renderObjectID || !mesh)
			return false;

		// Scene-object identity, rather than MeshHandle, owns instance state. This
		// allows many entities to share one mesh asset and its BLAS.
		if (m_MeshRegister.RegisteredMeshInstances.find(renderObjectID) != m_MeshRegister.RegisteredMeshInstances.end())
			return false;

		Ref<MeshSource> meshSource = mesh->GetMeshSource();
		if (!meshSource)
			return false;

		MeshSourceHandle meshSourceAssetHandle = meshSource->Handle;
		Handle<MeshSourceProxy> meshProxyHandle;

		if (m_MeshRegister.MeshSourceRegistered.find(meshSourceAssetHandle) == m_MeshRegister.MeshSourceRegistered.end())
		{
			const auto& indices = meshSource->GetIndices();
			const auto& vertices = meshSource->GetVertices();
			const auto& submeshes = meshSource->GetSubmeshes();

			meshProxyHandle = ResourceManager::CreateMeshSourceProxy(
				vertices.data(),
				static_cast<uint32_t>(vertices.size() * sizeof(MeshSource::Vertex)),
				indices.data(),
				static_cast<uint32_t>(indices.size() * sizeof(MeshSource::Index)),
				submeshes);

			m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle] = meshProxyHandle;
			m_MeshRegister.ASRegisterInfo.BLASToBuild.push_back(meshProxyHandle);
			m_MeshRegister.MeshSourceReferenceCounts[meshSourceAssetHandle] = 0;

			MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
			CORE_ASSERT(meshSourceProxy, "Invalid MeshSourceProxy");

			std::unordered_map<uint32_t, std::vector<Handle<MeshInstanceProxy>>> submeshInstances;
			submeshInstances.reserve(meshSourceProxy->Submeshes.size());
			for (uint32_t i = 0; i < meshSourceProxy->Submeshes.size(); i++)
				submeshInstances[i] = std::vector<Handle<MeshInstanceProxy>>();

			m_MeshRegister.MeshInstanceMap[meshProxyHandle] = submeshInstances;
		}
		else
		{
			meshProxyHandle = m_MeshRegister.MeshSourceRegistered[meshSourceAssetHandle];
		}

		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
		CORE_ASSERT(meshSourceProxy, "Invalid MeshSourceProxy");

		const auto& submeshesProxy = meshSourceProxy->Submeshes;
		auto [registeredIt, inserted] = m_MeshRegister.RegisteredMeshInstances.emplace(
			renderObjectID,
			MeshRegister::RegisteredMeshInstance{});
		CORE_ASSERT(inserted, "RenderObjectID was registered more than once");

		MeshRegister::RegisteredMeshInstance& registeredInstance = registeredIt->second;
		registeredInstance.MeshAssetHandle = mesh->Handle;
		registeredInstance.MeshSourceAssetHandle = meshSourceAssetHandle;
		registeredInstance.SubmeshInstances.reserve(submeshesProxy.size());
		m_MeshRegister.MeshSourceReferenceCounts[meshSourceAssetHandle]++;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		m_MeshRegister.ASRegisterInfo.TLASNeedsSafeUpdate = true;

		const Ref<MaterialTable> effectiveMaterialTable = materialTable ? materialTable : mesh->GetMaterialTable();

		for (uint32_t submeshIndex = 0; submeshIndex < submeshesProxy.size(); submeshIndex++)
		{
			const uint32_t materialSlot = submeshesProxy[submeshIndex].MaterialIndex;
			Ref<MaterialAsset> materialAsset = ResolveMaterialAsset(meshSource, effectiveMaterialTable, materialSlot);
			const uint32_t materialProxyIndex = RegisterMaterialProxy(materialAsset);

			Handle<MeshInstanceProxy> meshInstanceHandle = ResourceManager::CreateMeshInstanceProxy(
				meshProxyHandle,
				submeshIndex,
				materialProxyIndex);
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			CORE_ASSERT(meshInstance, "Invalid MeshInstanceProxy");

			meshInstance->WorldTransform = transform * submeshesProxy[submeshIndex].Transform;

			registeredInstance.SubmeshInstances.push_back(meshInstanceHandle);
			m_MeshRegister.MeshInstanceMap[meshProxyHandle][submeshIndex].push_back(meshInstanceHandle);
			m_MeshRegister.RasterMeshInstances.push_back(meshInstanceHandle);
			m_MeshRegister.MeshInstanceCount++;
		}

		return true;
	}

	bool RenderScene::UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable)
	{
		if (!renderObjectID)
			return false;

		auto registeredMesh = m_MeshRegister.RegisteredMeshInstances.find(renderObjectID);
		if (registeredMesh == m_MeshRegister.RegisteredMeshInstances.end())
			return false;

		Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(registeredMesh->second.MeshAssetHandle);
		if (!mesh)
			return false;

		const Ref<MeshSource> meshSource = mesh->GetMeshSource();
		if (!meshSource)
			return false;

		const Ref<MaterialTable> effectiveMaterialTable = materialTable ? materialTable : mesh->GetMaterialTable();
		for (Handle<MeshInstanceProxy> meshInstanceHandle : registeredMesh->second.SubmeshInstances)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			if (!meshInstance)
				continue;

			MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);
			if (!meshSourceProxy || meshInstance->SubmeshIndex >= meshSourceProxy->Submeshes.size())
				continue;

			const uint32_t materialSlot = meshSourceProxy->Submeshes[meshInstance->SubmeshIndex].MaterialIndex;
			const Ref<MaterialAsset> materialAsset = ResolveMaterialAsset(meshSource, effectiveMaterialTable, materialSlot);
			const uint32_t materialProxyIndex = RegisterMaterialProxy(materialAsset);
			meshInstance->MaterialProxyIndex = materialProxyIndex;

			if (meshInstance->RayTracingInstanceIndex != InvalidRayTracingInstanceIndex &&
				meshInstance->RayTracingInstanceIndex < m_RayTracingInstances.size())
			{
				const MaterialProxy* materialProxy = GetMaterialProxy(materialProxyIndex);
				CORE_ASSERT(materialProxy, "Updated mesh material has no MaterialProxy");
				m_RayTracingInstances[meshInstance->RayTracingInstanceIndex].MaterialIndex = materialProxy->GPUMaterialIndex;
			}
		}

		return true;
	}

	bool RenderScene::UnregisterMesh(RenderObjectID renderObjectID)
	{
		if (!renderObjectID)
			return false;

		auto registeredIt = m_MeshRegister.RegisteredMeshInstances.find(renderObjectID);
		if (registeredIt == m_MeshRegister.RegisteredMeshInstances.end())
			return false;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		m_MeshRegister.ASRegisterInfo.TLASNeedsSafeUpdate = true;

		MeshRegister::RegisteredMeshInstance registeredInstance = std::move(registeredIt->second);
		m_MeshRegister.RegisteredMeshInstances.erase(registeredIt);

		auto meshSourceIt = m_MeshRegister.MeshSourceRegistered.find(registeredInstance.MeshSourceAssetHandle);
		CORE_ASSERT(meshSourceIt != m_MeshRegister.MeshSourceRegistered.end(), "Registered mesh instance has no MeshSourceProxy");
		const Handle<MeshSourceProxy> meshSourceHandle = meshSourceIt->second;

		for (Handle<MeshInstanceProxy> meshInstanceHandle : registeredInstance.SubmeshInstances)
		{
			const auto rasterInstanceIt = std::find(
				m_MeshRegister.RasterMeshInstances.begin(),
				m_MeshRegister.RasterMeshInstances.end(),
				meshInstanceHandle);
			if (rasterInstanceIt != m_MeshRegister.RasterMeshInstances.end())
				m_MeshRegister.RasterMeshInstances.erase(rasterInstanceIt);

			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			if (meshInstance)
			{
				auto& instancesPerSubmesh = m_MeshRegister.MeshInstanceMap.at(meshSourceHandle).at(meshInstance->SubmeshIndex);
				const auto instanceIt = std::find(instancesPerSubmesh.begin(), instancesPerSubmesh.end(), meshInstanceHandle);
				if (instanceIt != instancesPerSubmesh.end())
				{
					instancesPerSubmesh.erase(instanceIt);
					CORE_ASSERT(m_MeshRegister.MeshInstanceCount > 0, "MeshInstanceCount underflow");
					m_MeshRegister.MeshInstanceCount--;
				}
			}

			ResourceManager::DestroyMeshInstanceProxy(meshInstanceHandle);
		}

		auto referenceCountIt = m_MeshRegister.MeshSourceReferenceCounts.find(registeredInstance.MeshSourceAssetHandle);
		CORE_ASSERT(referenceCountIt != m_MeshRegister.MeshSourceReferenceCounts.end(), "Registered mesh source has no reference count");
		CORE_ASSERT(referenceCountIt->second > 0, "MeshSource reference count underflow");
		referenceCountIt->second--;

		if (referenceCountIt->second == 0)
		{
			m_MeshRegister.MeshInstanceMap.erase(meshSourceHandle);
			m_MeshRegister.MeshSourceReferenceCounts.erase(referenceCountIt);
			m_MeshRegister.MeshSourceRegistered.erase(registeredInstance.MeshSourceAssetHandle);

			if (MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshSourceHandle))
			{
				for (auto& submesh : meshSourceProxy->Submeshes)
				{
					if (submesh.BLAS.IsValid())
					{
						m_MeshRegister.ASRegisterInfo.PendingBLASToDestroy.push_back(submesh.BLAS);
						submesh.BLAS = Handle<AccelerationStructure>{};
					}
				}
			}

			m_MeshRegister.ASRegisterInfo.PendingMeshSourcesToDestroy.push_back(meshSourceHandle);

			// The previous TLAS may still reference the BLAS from this mesh source.
			// The next TLAS rebuild must first safely retire the old TLAS/BLAS.
			m_MeshRegister.ASRegisterInfo.ASNeedsSafeRebuild = true;
		}

		return true;
	}

	void RenderScene::UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform)
	{
		auto registeredIt = m_MeshRegister.RegisteredMeshInstances.find(renderObjectID);
		if (registeredIt == m_MeshRegister.RegisteredMeshInstances.end())
			return;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		m_MeshRegister.ASRegisterInfo.TLASNeedsSafeUpdate = true;

		const MeshRegister::RegisteredMeshInstance& registeredInstance = registeredIt->second;
		const Handle<MeshSourceProxy> meshSourceHandle = m_MeshRegister.MeshSourceRegistered.at(registeredInstance.MeshSourceAssetHandle);
		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshSourceHandle);
		CORE_ASSERT(meshSourceProxy, "Invalid MeshSourceProxy");

		for (Handle<MeshInstanceProxy> meshInstanceHandle : registeredInstance.SubmeshInstances)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			CORE_ASSERT(meshInstance, "Invalid MeshInstanceProxy");
			CORE_ASSERT(meshInstance->SubmeshIndex < meshSourceProxy->Submeshes.size(), "MeshInstanceProxy references an invalid submesh");

			const SubmeshSourceProxy& submeshProxy = meshSourceProxy->Submeshes[meshInstance->SubmeshIndex];
			meshInstance->WorldTransform = transform * submeshProxy.Transform;
		}
	}

	bool RenderScene::IsRegistered(RenderObjectID renderObjectID) const
	{
		if (!renderObjectID)
			return false;

		return m_MeshRegister.RegisteredMeshInstances.find(renderObjectID) != m_MeshRegister.RegisteredMeshInstances.end();
	}

	bool RenderScene::PrepareRayTracing()
	{
		BuildPendingBLAS();
		BuildOrUpdateTLAS();
		return HasValidTLAS();
	}

	void RenderScene::BuildPendingBLAS()
	{
		auto& asInfo = m_MeshRegister.ASRegisterInfo;
		if (asInfo.BLASToBuild.empty())
			return;

		NV_CORE_INFO("RT BLASToBuild: {}", asInfo.BLASToBuild.size());

		AccelerationStructureBuilder asBuilder;
		asBuilder.BeginBLASBuild(
			VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
			VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
			VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

		for (uint32_t i = 0; i < asInfo.BLASToBuild.size(); i++)
			asBuilder.SummitMesh(asInfo.BLASToBuild[i]);

		asBuilder.EndBLASBuild();
		asInfo.BLASToBuild.clear();
	}

	void RenderScene::BuildOrUpdateTLAS()
	{
		auto& asInfo = m_MeshRegister.ASRegisterInfo;

		if (asInfo.TLAS.IsValid() && !asInfo.UpdateTLAS)
			return;

		NV_CORE_INFO("RT MeshInstanceCount: {}", m_MeshRegister.MeshInstanceCount);
		NV_CORE_INFO("RT RegisteredMeshInstances: {}", m_MeshRegister.RegisteredMeshInstances.size());
		NV_CORE_INFO("RT TLAS valid before build: {}", asInfo.TLAS.IsValid());
		NV_CORE_INFO("RT UpdateTLAS: {}", asInfo.UpdateTLAS);

		if (asInfo.ASNeedsSafeRebuild)
		{
			FlushPendingRTASDeletes();
		}
		else if (asInfo.TLASNeedsSafeUpdate && asInfo.TLAS.IsValid())
		{
			// TODO RT:
			// Debug-safe only. This stalls the GPU.
			// Replace with frame-fence based TLAS double/triple buffering later.
			vkDeviceWaitIdle(RendererContext::GetCurrentVulkanDevice());
			DeferredResourceDeletionQueue::FlushAll();
		}

		if (m_MeshRegister.MeshInstanceCount == 0)
		{
			m_RayTracingInstances.clear();
			ResetRayTracingDirtyFlags();
			return;
		}

		AccelerationStructureBuilder asBuilder;
		asBuilder.BeginTLASBuild(
			VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
			VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

		uint32_t submittedTLASInstances = 0;
		m_RayTracingInstances.clear();
		m_RayTracingInstances.reserve(m_MeshRegister.MeshInstanceCount);

		for (const auto& [renderObjectID, registeredInstance] : m_MeshRegister.RegisteredMeshInstances)
		{
			(void)renderObjectID;
			for (Handle<MeshInstanceProxy> meshInstanceHandle : registeredInstance.SubmeshInstances)
			{
				MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
				CORE_ASSERT(meshInstance, "Invalid MeshInstanceProxy while building TLAS instance data");

				const MaterialProxy* materialProxy = GetMaterialProxy(meshInstance->MaterialProxyIndex);
				CORE_ASSERT(materialProxy, "MeshInstanceProxy references an invalid MaterialProxy");
				CORE_ASSERT(materialProxy->GPUMaterialIndex != InvalidMaterialProxyIndex, "MaterialProxy has no GPU material index");

				MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshInstance->MeshSource);
				CORE_ASSERT(meshSourceProxy, "MeshInstanceProxy references an invalid MeshSourceProxy");
				CORE_ASSERT(meshInstance->SubmeshIndex < meshSourceProxy->Submeshes.size(), "MeshInstanceProxy references an invalid submesh");

				GPUBuffer* vertexBuffer = ResourceManager::GetBuffer(meshSourceProxy->VertexBuffer->GetGPUBuffer());
				GPUBuffer* indexBuffer = ResourceManager::GetBuffer(meshSourceProxy->IndexBuffer->GetGPUBuffer());
				CORE_ASSERT(vertexBuffer && indexBuffer, "RT mesh has invalid geometry buffers");

				const VkDeviceAddress vertexBufferAddress = vertexBuffer->GetDeviceAddress();
				const VkDeviceAddress indexBufferAddress = indexBuffer->GetDeviceAddress();
				CORE_ASSERT(vertexBufferAddress != 0, "RT vertex buffer has no device address");
				CORE_ASSERT(indexBufferAddress != 0, "RT index buffer has no device address");
				CORE_ASSERT((vertexBufferAddress & 7ull) == 0, "RT vertex buffer address is not 8-byte aligned");
				CORE_ASSERT((indexBufferAddress & 7ull) == 0, "RT index buffer address is not 8-byte aligned");

				const SubmeshSourceProxy& submesh = meshSourceProxy->Submeshes[meshInstance->SubmeshIndex];

				const uint32_t instanceCustomIndex = static_cast<uint32_t>(m_RayTracingInstances.size());
				meshInstance->RayTracingInstanceIndex = instanceCustomIndex;
				RayTracingInstanceData& instanceData = m_RayTracingInstances.emplace_back();
				instanceData.VertexBufferAddress = vertexBufferAddress;
				instanceData.IndexBufferAddress = indexBufferAddress;
				instanceData.MaterialIndex = materialProxy->GPUMaterialIndex;
				instanceData.FirstIndex = submesh.BaseIndex;
				instanceData.BaseVertex = submesh.BaseVertex;

				asBuilder.SubmitMeshInstance(meshInstanceHandle, instanceCustomIndex);
				submittedTLASInstances++;
			}
		}

		CORE_ASSERT(m_RayTracingInstances.size() == submittedTLASInstances, "RT instance data is out of sync with TLAS instances");
		NV_CORE_INFO("RT submitted TLAS instances: {}", submittedTLASInstances);

		asBuilder.EndTLASBuild(asInfo.TLAS);
		ResetRayTracingDirtyFlags();
	}

	void RenderScene::FlushPendingRTASDeletes()
	{
		auto& asInfo = m_MeshRegister.ASRegisterInfo;

		if (!asInfo.ASNeedsSafeRebuild &&
			asInfo.PendingBLASToDestroy.empty() &&
			asInfo.PendingMeshSourcesToDestroy.empty())
		{
			return;
		}

		// TODO: Debug-safe version. This guarantees that no command buffer still references
		// the old TLAS/BLAS before we destroy them. Replace this later with a proper
		// frame-fence deferred deletion queue.
		vkDeviceWaitIdle(RendererContext::GetCurrentVulkanDevice());
		DeferredResourceDeletionQueue::FlushAll();

		// If BLAS were invalidated by unregistering a mesh source, the current TLAS may
		// still contain instance records pointing at those BLAS. Destroy the TLAS first,
		// then rebuild it from the remaining live instances in PrepareRayTracing().
		if (asInfo.TLAS.IsValid())
		{
			ResourceManager::DestroyAccelerationStructure(asInfo.TLAS);
			asInfo.TLAS = Handle<AccelerationStructure>{};
		}

		for (auto& blas : asInfo.PendingBLASToDestroy)
		{
			if (blas.IsValid())
				ResourceManager::DestroyAccelerationStructure(blas);
		}

		for (auto& meshSource : asInfo.PendingMeshSourcesToDestroy)
		{
			if (meshSource.IsValid())
				ResourceManager::DestroyMeshSourceProxy(meshSource);
		}

		asInfo.PendingBLASToDestroy.clear();
		asInfo.PendingMeshSourcesToDestroy.clear();
		asInfo.ASNeedsSafeRebuild = false;
	}

	void RenderScene::DestroyAllMeshProxies()
	{
		// Destroy all instance proxies first.
		for (auto& [renderObjectID, registeredInstance] : m_MeshRegister.RegisteredMeshInstances)
		{
			(void)renderObjectID;
			for (Handle<MeshInstanceProxy> meshInstance : registeredInstance.SubmeshInstances)
			{
				if (meshInstance.IsValid())
					ResourceManager::DestroyMeshInstanceProxy(meshInstance);
			}
		}

		// Destroy any BLAS that are still owned by live mesh source proxies.
		for (auto& [meshSourceHandle, meshSourceProxyHandle] : m_MeshRegister.MeshSourceRegistered)
		{
			(void)meshSourceHandle;
			if (MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshSourceProxyHandle))
			{
				for (auto& submesh : meshSourceProxy->Submeshes)
				{
					if (submesh.BLAS.IsValid())
					{
						ResourceManager::DestroyAccelerationStructure(submesh.BLAS);
						submesh.BLAS = Handle<AccelerationStructure>{};
					}
				}
			}

			if (meshSourceProxyHandle.IsValid())
				ResourceManager::DestroyMeshSourceProxy(meshSourceProxyHandle);
		}

		m_MeshRegister.RegisteredMeshInstances.clear();
		m_MeshRegister.MeshSourceRegistered.clear();
		m_MeshRegister.MeshSourceReferenceCounts.clear();
		ClearMaterialProxies();
		m_RayTracingInstances.clear();
		m_MeshRegister.MeshInstanceMap.clear();
		m_MeshRegister.RasterMeshInstances.clear();
		m_MeshRegister.MeshInstanceCount = 0;
	}

	void RenderScene::ResetRayTracingDirtyFlags()
	{
		auto& asInfo = m_MeshRegister.ASRegisterInfo;
		asInfo.UpdateTLAS = false;
		asInfo.TLASNeedsSafeUpdate = false;
		asInfo.ASNeedsSafeRebuild = false;
	}

	void RenderScene::Shutdown()
	{
		auto& asInfo = m_MeshRegister.ASRegisterInfo;

		// If no renderer resources are registered, this is a no-op.
		if (!asInfo.TLAS.IsValid() &&
			asInfo.BLASToBuild.empty() &&
			asInfo.PendingBLASToDestroy.empty() &&
			asInfo.PendingMeshSourcesToDestroy.empty() &&
			m_MeshRegister.RegisteredMeshInstances.empty() &&
			m_MeshRegister.MeshSourceRegistered.empty() &&
			m_MeshRegister.MaterialProxies.empty() &&
			m_MeshRegister.GPUTextures.empty() &&
			m_RayTracingInstances.empty())
		{
			return;
		}

		// Debug-safe shutdown: make sure no submitted command buffer still references
		// TLAS/BLAS resources before destroying them.
		vkDeviceWaitIdle(RendererContext::GetCurrentVulkanDevice());
		DeferredResourceDeletionQueue::FlushAll();

		FlushPendingRTASDeletes();

		if (asInfo.TLAS.IsValid())
		{
			ResourceManager::DestroyAccelerationStructure(asInfo.TLAS);
			asInfo.TLAS = Handle<AccelerationStructure>{};
		}

		for (auto& blas : asInfo.PendingBLASToDestroy)
		{
			if (blas.IsValid())
				ResourceManager::DestroyAccelerationStructure(blas);
		}
		asInfo.PendingBLASToDestroy.clear();

		for (auto& meshSource : asInfo.PendingMeshSourcesToDestroy)
		{
			if (meshSource.IsValid())
				ResourceManager::DestroyMeshSourceProxy(meshSource);
		}
		asInfo.PendingMeshSourcesToDestroy.clear();

		DestroyAllMeshProxies();

		asInfo.BLASToBuild.clear();
		ResetRayTracingDirtyFlags();
	}
}

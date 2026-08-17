#include "pch.h"
#include "RenderScene.h"

#include "Renderer/DeferredResourceDeletionQueue.h"
#include "Renderer/RendererContext.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RendererResources.h"
#include "Renderer/Texture.h"
#include "Renderer/TextureAsset.h"
#include "Asset/AssetManager.h"

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

		CORE_ASSERT(m_MeshRegister.GPUTextures.size() < MaxRayTracingTextureCount, "RT texture table capacity exceeded");

		Handle<Texture2D> gpuTexture = ResourceManager::CreateTexture2D(
			textureAsset,
			srgb ? TextureColorSpace::SRGB : TextureColorSpace::Linear);
		CORE_ASSERT(gpuTexture.IsValid(), "Failed to create GPU texture for RT material table");

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

	bool RenderScene::RegisterMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform)
	{
		if (!mesh)
			return false;

		// If the mesh is already registered return false.
		if (m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) != m_MeshRegister.MeshInstanceRegistered.end())
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
			m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAssetHandle] = {};

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

		m_MeshRegister.InstancePerMeshSourceRegistered[meshSourceAssetHandle].push_back(mesh->Handle);

		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshProxyHandle);
		CORE_ASSERT(meshSourceProxy, "Invalid MeshSourceProxy");

		const auto& submeshesProxy = meshSourceProxy->Submeshes;
		auto& meshInstanceProxies = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];

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

			meshInstanceProxies.push_back(meshInstanceHandle);
			m_MeshRegister.MeshInstanceMap[meshProxyHandle][submeshIndex].push_back(meshInstanceHandle);
			m_MeshRegister.MeshInstanceCount++;
		}

		return true;
	}

	bool RenderScene::UnregisterMesh(const Ref<Mesh>& mesh)
	{
		if (!mesh)
			return false;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		m_MeshRegister.ASRegisterInfo.TLASNeedsSafeUpdate = true;

		auto registeredIt = m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle);
		if (registeredIt == m_MeshRegister.MeshInstanceRegistered.end())
			return false;

		std::vector<Handle<MeshInstanceProxy>> meshInstanceProxyList = registeredIt->second;
		m_MeshRegister.MeshInstanceRegistered.erase(registeredIt);

		Ref<MeshSource> meshSourceAsset = mesh->GetMeshSource();
		CORE_ASSERT(meshSourceAsset, "Invalid MeshSource");

		auto instanceListIt = m_MeshRegister.InstancePerMeshSourceRegistered.find(meshSourceAsset->Handle);
		if (instanceListIt == m_MeshRegister.InstancePerMeshSourceRegistered.end())
			return true;

		auto& instanceList = instanceListIt->second;
		instanceList.erase(std::remove(instanceList.begin(), instanceList.end(), mesh->Handle), instanceList.end());

		Handle<MeshSourceProxy> meshSourceHandle = m_MeshRegister.MeshSourceRegistered[meshSourceAsset->Handle];

		for (Handle<MeshInstanceProxy> meshInstanceHandle : meshInstanceProxyList)
		{
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstanceHandle);
			if (meshInstance)
			{
				auto& instancesPerSubmesh = m_MeshRegister.MeshInstanceMap[meshSourceHandle][meshInstance->SubmeshIndex];
				for (uint32_t index = 0; index < instancesPerSubmesh.size(); index++)
				{
					Handle<MeshInstanceProxy> currentMeshInstance = instancesPerSubmesh[index];
					if (meshInstanceHandle == currentMeshInstance)
					{
						instancesPerSubmesh.erase(instancesPerSubmesh.begin() + index);
						m_MeshRegister.MeshInstanceCount--;
						break;
					}
				}
			}

			ResourceManager::DestroyMeshInstanceProxy(meshInstanceHandle);
		}

		if (instanceList.empty())
		{
			m_MeshRegister.MeshInstanceMap.erase(meshSourceHandle);
			m_MeshRegister.InstancePerMeshSourceRegistered.erase(meshSourceAsset->Handle);
			m_MeshRegister.MeshSourceRegistered.erase(meshSourceAsset->Handle);

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

	void RenderScene::UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!IsRegistered(mesh))
			return;

		m_MeshRegister.ASRegisterInfo.UpdateTLAS = true;
		m_MeshRegister.ASRegisterInfo.TLASNeedsSafeUpdate = true;

		std::vector<Handle<MeshInstanceProxy>> meshInstances = m_MeshRegister.MeshInstanceRegistered[mesh->Handle];
		Handle<MeshSourceProxy> meshSourceHandle = m_MeshRegister.MeshSourceRegistered[mesh->GetMeshSource()->Handle];
		MeshSourceProxy* meshSourceProxy = ResourceManager::GetMeshSourceProxy(meshSourceHandle);
		CORE_ASSERT(meshSourceProxy, "Invalid MeshSourceProxy");

		for (uint32_t submeshIndex = 0; submeshIndex < meshSourceProxy->Submeshes.size(); submeshIndex++)
		{
			const SubmeshSourceProxy& submeshProxy = meshSourceProxy->Submeshes[submeshIndex];
			MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstances[submeshIndex]);
			CORE_ASSERT(meshInstance, "Invalid MeshInstanceProxy");

			meshInstance->WorldTransform = transform * submeshProxy.Transform;
		}
	}

	bool RenderScene::IsRegistered(const Ref<Mesh>& mesh) const
	{
		if (!mesh)
			return false;

		return m_MeshRegister.MeshInstanceRegistered.find(mesh->Handle) != m_MeshRegister.MeshInstanceRegistered.end();
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
		NV_CORE_INFO("RT MeshInstanceRegistered: {}", m_MeshRegister.MeshInstanceRegistered.size());
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

		for (const auto& [meshAssetHandle, meshInstances] : m_MeshRegister.MeshInstanceRegistered)
		{
			(void)meshAssetHandle;
			for (uint32_t i = 0; i < meshInstances.size(); i++)
			{
				MeshInstanceProxy* meshInstance = ResourceManager::GetMeshInstanceProxy(meshInstances[i]);
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
				RayTracingInstanceData& instanceData = m_RayTracingInstances.emplace_back();
				instanceData.VertexBufferAddress = vertexBufferAddress;
				instanceData.IndexBufferAddress = indexBufferAddress;
				instanceData.MaterialIndex = materialProxy->GPUMaterialIndex;
				instanceData.FirstIndex = submesh.BaseIndex;
				instanceData.BaseVertex = submesh.BaseVertex;

				asBuilder.SubmitMeshInstance(meshInstances[i], instanceCustomIndex);
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
		for (auto& [meshHandle, meshInstances] : m_MeshRegister.MeshInstanceRegistered)
		{
			(void)meshHandle;
			for (Handle<MeshInstanceProxy> meshInstance : meshInstances)
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

		m_MeshRegister.MeshInstanceRegistered.clear();
		m_MeshRegister.MeshSourceRegistered.clear();
		m_MeshRegister.InstancePerMeshSourceRegistered.clear();
		ClearMaterialProxies();
		m_RayTracingInstances.clear();
		m_MeshRegister.MeshInstanceMap.clear();
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
			m_MeshRegister.MeshInstanceRegistered.empty() &&
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

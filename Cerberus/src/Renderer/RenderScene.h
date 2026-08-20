#pragma once

#include "Core/Handle.h"
#include "Renderer/AccelerationStructure.h"
#include "Renderer/Resources/MeshProxy.h"
#include "Renderer/Resources/MaterialProxy.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Scene/SceneEnvironment.h"

#include <array>
#include <span>

namespace cerberus
{
	class Texture2D;
	class TextureCube;
	struct EnvironmentMap;

	static constexpr uint32_t MaxRayTracingTextureCount = 128;

	static_assert(sizeof(MeshSource::Vertex) == 14 * sizeof(float), "RT vertex fetch expects MeshSource::Vertex to contain 14 tightly packed floats");
	static_assert(sizeof(MeshSource::Index) == 3 * sizeof(uint32_t), "RT index fetch expects tightly packed uint32 triangle indices");

	// GPU-facing data indexed by gl_InstanceCustomIndexEXT. There is one entry for
	// every TLAS instance; Cerberus currently creates one TLAS instance per submesh.
	struct RayTracingInstanceData
	{
		uint64_t VertexBufferAddress = 0;
		uint64_t IndexBufferAddress = 0;
		uint32_t MaterialIndex = 0;
		uint32_t FirstIndex = 0;
		uint32_t BaseVertex = 0;
		uint32_t Reserved = 0;
	};

	static_assert(sizeof(RayTracingInstanceData) == 32, "RayTracingInstanceData must match the std430 shader layout");

	struct SkyLightProxy
	{
		Handle<TextureCube> RadianceMap;
		Handle<TextureCube> IrradianceMap;
		float Intensity = 1.0f;
		float SampleLOD = 0.0f;
	};

	struct DirectionalLightProxy
	{
		// Direction points from the shaded surface toward the light, matching the
		// existing Scene/forward-renderer convention.
		glm::vec3 Direction = glm::vec3(0.0f);
		float Intensity = 0.0f;
		glm::vec3 Radiance = glm::vec3(0.0f);
		float Padding = 0.0f;
	};

	// RenderScene is the renderer-side representation of the logical Scene.
	// It owns render proxies, GPU-facing mesh registrations, and RT acceleration
	// structure state. SceneRenderer implementations consume this data instead of
	// carrying scene registry state themselves.
	class RenderScene final
	{
	public:
		RenderScene() = default;
		~RenderScene();

		bool RegisterMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform);
		bool UnregisterMesh(const Ref<Mesh>& mesh);
		void UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform);
		bool UpdateMeshMaterials(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable);
		bool UpdateMaterialProxy(const Ref<MaterialAsset>& materialAsset);
		void SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD);
		bool SetDirectionalLights(std::span<const DirectionalLight> directionalLights);
		bool IsRegistered(const Ref<Mesh>& mesh) const;

		// Builds pending BLAS and builds/updates TLAS when dirty.
		// Returns true when a valid TLAS is available for tracing.
		bool PrepareRayTracing();

		bool HasRenderableMeshes() const { return m_MeshRegister.MeshInstanceCount > 0; }
		bool HasValidTLAS() const { return m_MeshRegister.ASRegisterInfo.TLAS.IsValid(); }
		Handle<AccelerationStructure> GetTLAS() const { return m_MeshRegister.ASRegisterInfo.TLAS; }

		// Render-friendly material table. MeshInstanceProxy::MaterialProxyIndex indexes this array.
		const std::vector<MaterialProxy>& GetMaterialProxies() const { return m_MeshRegister.MaterialProxies; }
		const MaterialProxy* GetMaterialProxy(uint32_t materialProxyIndex) const;
		const std::vector<Handle<Texture2D>>& GetRayTracingTextures() const { return m_MeshRegister.GPUTextures; }
		const std::vector<RayTracingInstanceData>& GetRayTracingInstances() const { return m_RayTracingInstances; }
		const SkyLightProxy& GetSkyLight() const { return m_SkyLight; }
		const std::array<DirectionalLightProxy, MaxDirectionalLightCount>& GetDirectionalLights() const { return m_DirectionalLights; }
		uint32_t GetDirectionalLightCount() const { return m_DirectionalLightCount; }

		// Explicit shutdown is useful because the owning SceneRenderer can decide to
		// wait for the GPU before releasing render-scene resources. Safe to call more
		// than once.
		void Shutdown();

	private:
		Ref<MaterialAsset> ResolveMaterialAsset(
			const Ref<MeshSource>& meshSource,
			const Ref<MaterialTable>& materialTable,
			uint32_t materialSlot) const;
		uint32_t RegisterMaterialProxy(const Ref<MaterialAsset>& materialAsset);
		uint32_t RegisterTextureProxy(AssetHandle textureAsset, bool srgb);
		GPUMaterialData BuildMaterialGPUData(const Ref<MaterialAsset>& materialAsset);
		void ClearMaterialProxies();
		void ClearTextureProxies();

		void BuildPendingBLAS();
		void BuildOrUpdateTLAS();
		void FlushPendingRTASDeletes();
		void DestroyAllMeshProxies();
		void ResetRayTracingDirtyFlags();

	private:
		struct MeshRegister
		{
			struct AccelerationStructureInfo
			{
				std::vector<Handle<MeshSourceProxy>> BLASToBuild;
				std::vector<Handle<AccelerationStructure>> PendingBLASToDestroy;
				std::vector<Handle<MeshSourceProxy>> PendingMeshSourcesToDestroy;

				bool UpdateTLAS = false;

				// BLAS/MeshSource invalidated. Requires safe rebuild + pending deletes.
				bool ASNeedsSafeRebuild = false;

				// Only TLAS contents changed, e.g. transform update.
				// Does not destroy BLAS, but still cannot update/rebuild TLAS while GPU uses old TLAS.
				bool TLASNeedsSafeUpdate = false;

				Handle<AccelerationStructure> TLAS;
			} ASRegisterInfo;

			std::unordered_map<MeshHandle, std::vector<Handle<MeshInstanceProxy>>> MeshInstanceRegistered;
			std::unordered_map<MeshSourceHandle, Handle<MeshSourceProxy>> MeshSourceRegistered;
			std::unordered_map<MeshHandle, std::vector<MeshHandle>> InstancePerMeshSourceRegistered;

			// MaterialAsset handle -> RenderScene-local MaterialProxy table index.
			// The default material is stored separately because it is a renderer resource and
			// therefore intentionally has no persistent AssetHandle.
			std::unordered_map<AssetHandle, uint32_t> MaterialRegistered;
			std::vector<MaterialProxy> MaterialProxies;
			uint32_t DefaultMaterialProxyIndex = InvalidMaterialProxyIndex;

			// The same source asset can be resident once as sRGB data and once as
			// linear data, because the material slot decides the color space.
			std::unordered_map<AssetHandle, uint32_t> SRGBTextureRegistered;
			std::unordered_map<AssetHandle, uint32_t> LinearTextureRegistered;
			std::vector<Handle<Texture2D>> GPUTextures;

			// Mesh Source Proxy -> Submesh index -> Submesh instance
			std::unordered_map<Handle<MeshSourceProxy>, std::unordered_map<uint32_t, std::vector<Handle<MeshInstanceProxy>>>> MeshInstanceMap;

			uint32_t MeshInstanceCount = 0;
		} m_MeshRegister;

		// Kept in exactly the same order as the instances submitted to the TLAS.
		std::vector<RayTracingInstanceData> m_RayTracingInstances;
		SkyLightProxy m_SkyLight;
		std::array<DirectionalLightProxy, MaxDirectionalLightCount> m_DirectionalLights{};
		uint32_t m_DirectionalLightCount = 0;
	};
}

#pragma once
#include "Renderer/GPUBuffer.h"
#include "Renderer/AccelerationStructure.h"
#include "Renderer/Backend/MeshProxy.h"
#include "Renderer/Backend/BindGroup.h"
#include "Core/Handle.h"
namespace cerberus
{
	struct RendererConfiguration
	{
		const uint32_t FramesInFlight = 3;
		const uint32_t BrdfLutSize = 256;
		const uint32_t EnvironmentMapResolution = 1024;
		const uint32_t IrradianceMapComputeSamples = 1024;
		const uint32_t IrradianceMapSize = 32;
		const uint32_t ShadowMapResolution = 4096;
		const uint32_t ShadowCascadesCount = 4;
	};

	class ResourceManager
	{
	public:
		static void Init();
		static void Shutdown();

		static Handle<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification);
		static void DestroyAccelerationStructure(Handle<AccelerationStructure>& as);
		static AccelerationStructure* GetAccelerationStructure(const Handle<AccelerationStructure>& as);
		
		static Handle<GPUBuffer> CreateBuffer(const GPUBufferSpecification& gpuBufferSpec);
		static void DestroyBuffer(const Handle<GPUBuffer>& buffer);
		static GPUBuffer* GetBuffer(const Handle<GPUBuffer>& buffe);

		static Handle<MeshSourceProxy> CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, const std::vector<Submesh>& submeshes);
		static void DestroyMeshSourceProxy(Handle<MeshSourceProxy>& meshSourceProxyHandle);
		static MeshSourceProxy* GetMeshSourceProxy(const Handle<MeshSourceProxy>& meshSourceProxyHandle);

		static Handle<MeshInstanceProxy> CreateMeshInstanceProxy(Handle<MeshSourceProxy> meshSource, uint32_t submeshIndex, Ref<MaterialAsset> material);
		static void DestroyMeshInstanceProxy(Handle<MeshInstanceProxy>& meshInstance);
		static MeshInstanceProxy* GetMeshInstanceProxy(const Handle<MeshInstanceProxy>& meshInstance);

		static Handle<BindGroupLayout> CreateBindGroupLayout(const BindGroupLayout::BindGroupLayoutInfo& info);
		static void DestroyBindGroupLayout(Handle<BindGroupLayout>& handle);
		static BindGroupLayout* GetBindGroupLayout(const Handle<BindGroupLayout>& handle);

		static const RendererConfiguration& GetRendererConfig();

		static VkDescriptorSet AllocateDescriptorSet(Handle<BindGroupLayout> Layout);

		static uint32_t GetFramesInFlight();

	};
}
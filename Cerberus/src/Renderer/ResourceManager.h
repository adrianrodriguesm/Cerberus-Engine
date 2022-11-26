#pragma once
#include "Renderer/GPUBuffer.h"
#include "Renderer/AccelerationStructure.h"
#include "Renderer/Backend/MeshProxy.h"
#include "Core/Handle.h"
namespace cerberus
{
	class ResourceManager
	{
	public:
		static void Init();
		static void Shutdown();

		static Handle<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureSpecification& asSpecification);
		static void DestroyAccelerationStructure(Handle<AccelerationStructure>& as);
		static AccelerationStructure* GetAccelerationStructure(const Handle<AccelerationStructure>& as);
		
		static Handle<GPUBuffer> CreateBuffer(const GPUBufferSpecification& gpuBufferSpec);
		static void DestroyBuffer(Handle<GPUBuffer>& buffer);
		static GPUBuffer* GetBuffer(const Handle<GPUBuffer>& buffe);

		static Handle<MeshSourceProxy> CreateMeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize);
		static void DestroyMeshSourceProxy(Handle<MeshSourceProxy>& meshSourceProxyHandle);
		static MeshSourceProxy* GetMeshSourceProxy(const Handle<MeshSourceProxy>& meshSourceProxyHandle);

		static Handle<MeshInstanceProxy> CreateMeshInstanceProxy(Handle<MeshSourceProxy> meshSource, uint32_t submeshIndex, Ref<MaterialAsset> material);
		static void DestroyMeshInstanceProxy(Handle<MeshInstanceProxy>& meshInstance);
		static MeshInstanceProxy* GetMeshInstanceProxy(const Handle<MeshInstanceProxy>& meshInstance);

	};
}
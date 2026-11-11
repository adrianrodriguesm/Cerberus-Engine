#pragma once
#include "Asset/Assets/MeshAsset.h"
#include "Core/Handle.h"
#include "Math/Matrix.h"
#include "Renderer/Scene/GeometryPool.h"
#include "Renderer/Scene/Proxies/MaterialProxy.h"

#include <span>
namespace cerberus
{
	class AccelerationStructure;
	static constexpr uint32_t InvalidRayTracingInstanceIndex = 0xFFFFFFFFu;

	struct SubmeshSourceProxy
	{
		uint32_t BaseVertex;
		uint32_t BaseIndex, IndexCount;
		uint32_t MaterialIndex;
		glm::mat4 Transform = math::MatFactory::CreateIdentityMat4();
		Handle<AccelerationStructure> BLAS;
		SubmeshSourceProxy() = default;
		SubmeshSourceProxy(uint32_t baseVertex, uint32_t baseIndex, uint32_t indexCount)
			: BaseVertex(baseVertex), BaseIndex(baseIndex), IndexCount(indexCount)
		{
		}
	};

	// Render Core representation of a mesh. Geometry lives in its owning
	// RenderScene's GeometryPool (one shared vertex/index buffer pair per
	// scene) rather than in a VertexBuffer/IndexBuffer of its own -- see
	// docs/tasks/T3-geometry-pool.md. GlobalVertexOffset/GlobalFirstIndex are
	// what a draw or a BLAS build actually indexes with; BaseVertex/BaseIndex
	// on the submesh stay relative to this mesh's own allocation, unchanged.
	struct MeshSourceProxy
	{
		GeometryAllocation Allocation;
		GeometryPool* Pool = nullptr; // non-owning; Pool->Free() happens in the destructor
		std::vector<SubmeshSourceProxy> Submeshes;

		MeshSourceProxy(MeshSourceProxy&& other) noexcept
			: Allocation(other.Allocation)
			, Pool(other.Pool)
			, Submeshes(std::move(other.Submeshes))
		{
			other.Pool = nullptr;
			other.Allocation = {};
		}

		MeshSourceProxy(GeometryPool& pool, const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, std::span<const Submesh> submeshes)
			: Pool(&pool)
		{
			CORE_ASSERT(indexSize % sizeof(uint32_t) == 0, "Index data size must be a whole number of uint32_t indices");
			const uint32_t vertexCount = vertexSize / GeometryPool::VertexStrideBytes;
			const uint32_t indexCount = indexSize / static_cast<uint32_t>(sizeof(uint32_t));

			Allocation = Pool->Allocate(vertexCount, indexCount);
			Pool->Upload(Allocation, vertexData, vertexSize, indexData, indexSize);

			// Add the submesh info
			Submeshes.reserve(submeshes.size());
			for (const auto& submesh : submeshes)
			{
				SubmeshSourceProxy& submeshProxy = Submeshes.emplace_back();
				submeshProxy.IndexCount = submesh.IndexCount;
				submeshProxy.BaseIndex = submesh.BaseIndex;
				submeshProxy.BaseVertex = submesh.BaseVertex;
				submeshProxy.MaterialIndex = submesh.MaterialIndex;
				submeshProxy.Transform = submesh.Transform;
			}
		}

		~MeshSourceProxy()
		{
			if (Pool)
				Pool->Free(Allocation);
		}

		uint32_t GlobalVertexOffset(const SubmeshSourceProxy& submesh) const { return Allocation.VertexOffset + submesh.BaseVertex; }
		uint32_t GlobalFirstIndex(const SubmeshSourceProxy& submesh) const { return Allocation.IndexOffset + submesh.BaseIndex; }
	};

	// Is basically a submesh instance
	struct MeshInstanceProxy
	{
		Handle<MeshSourceProxy> MeshSource;
		uint32_t SubmeshIndex;
		// Index into RenderScene's MaterialProxy table.
		uint32_t MaterialProxyIndex = InvalidMaterialProxyIndex;
		// Index used by gl_InstanceCustomIndexEXT in the current TLAS.
		uint32_t RayTracingInstanceIndex = InvalidRayTracingInstanceIndex;
		glm::mat4 WorldTransform;

		MeshInstanceProxy(const Handle<MeshSourceProxy>& meshSource, uint32_t submeshIndex, uint32_t materialProxyIndex,
			const glm::mat4& transform)
			: MeshSource(meshSource), SubmeshIndex(submeshIndex), MaterialProxyIndex(materialProxyIndex), WorldTransform(transform)
		{}

		MeshInstanceProxy(const Handle<MeshSourceProxy>& meshSource, uint32_t submeshIndex, uint32_t materialProxyIndex)
			: MeshSource(meshSource), SubmeshIndex(submeshIndex), MaterialProxyIndex(materialProxyIndex), WorldTransform(math::MatFactory::CreateIdentityMat4())
		{}
	};
}

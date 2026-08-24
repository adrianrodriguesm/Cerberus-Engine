#pragma once
#include "Renderer/RHI/Buffers/VertexBuffer.h"
#include "Asset/Assets/MeshAsset.h"
#include "Core/Handle.h"
#include "Math/Matrix.h"
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

	// Render Core representation of a mesh
	struct MeshSourceProxy
	{
		VertexBuffer* VertexBuffer;
		IndexBuffer*  IndexBuffer;
		std::vector<SubmeshSourceProxy> Submeshes;

		MeshSourceProxy(MeshSourceProxy&& other) noexcept
			: VertexBuffer(other.VertexBuffer)
			, IndexBuffer(other.IndexBuffer)
			, Submeshes(std::move(other.Submeshes))
		{
			other.VertexBuffer = nullptr;
			other.IndexBuffer = nullptr;
		}

		MeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, std::span<const Submesh> submeshes)
		{
			VertexBuffer = new cerberus::VertexBuffer(vertexData, vertexSize);
			IndexBuffer = new cerberus::IndexBuffer(indexData, indexSize);
			VertexBufferLayout layout = {{ ShaderDataType::Float3, "a_Position"	},
										 { ShaderDataType::Float3, "a_Normal"	},
										 { ShaderDataType::Float3, "a_Tangent"	},
										 { ShaderDataType::Float3, "a_Binormal"	},
										 { ShaderDataType::Float2, "a_TexCoord"	},
										};
			VertexBuffer->SetVertexBufferLayout(layout);
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
			if(VertexBuffer)
				delete VertexBuffer;
			if(IndexBuffer)
				delete IndexBuffer;
		}
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

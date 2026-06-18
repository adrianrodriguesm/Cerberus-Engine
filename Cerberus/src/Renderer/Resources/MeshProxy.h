#pragma once
#include "Renderer/VertexBuffer.h"
#include "Renderer/Mesh.h"
#include "Core/Handle.h"
#include "Math/Matrix.h"
#include "Renderer/Resources/BindGroup.h"
namespace cerberus
{
	struct SubmeshSourceProxy
	{
		uint32_t BaseVertex;
		uint32_t BaseIndex, IndexCount;
		uint32_t MaterialIndex;
		glm::mat4 Transform = math::MatFactory::CreateIdentityMat4();
		Handle<struct AccelerationStructure> BLAS;
		//std::vector<Handle<MeshInstanceProxy>> Instances;
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

		MeshSourceProxy(const void* vertexData, uint32_t vertexSize, const void* indexData, uint32_t indexSize, const std::vector<Submesh> submeshes)
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
		// TODO: Switch to binding group
		Handle<BindGroup> Material;
		glm::mat4 WorldTransform;

		MeshInstanceProxy(const Handle<MeshSourceProxy>& meshSource, const uint32_t submeshIndex, const Handle<BindGroup>& material,
			const glm::mat4& transform) : MeshSource(meshSource), SubmeshIndex(submeshIndex), Material(material), WorldTransform(transform)
		{}

		MeshInstanceProxy(const Handle<MeshSourceProxy>& meshSource, const uint32_t submeshIndex,
			const glm::mat4& transform) : MeshSource(meshSource), SubmeshIndex(submeshIndex), WorldTransform(transform)
		{}
	};
}
#pragma once
#include "Renderer/ResourceAllocator.h"
#include "Core/Buffer.h"
namespace cerberus
{
	/////////////////////////////////////////////////////////////////////// ShaderDataType
	enum class ShaderDataType
	{
		None = 0, Float, Float2, Float3, Float4, Mat3, Mat4, Int, Int2, Int3, Int4, Bool
	};
	///////////////////////////////////////////////////////////////////////
	static uint32_t ShaderDataTypeSize(ShaderDataType type)
	{
		switch (type)
		{
		case ShaderDataType::Float:	 return 4;
		case ShaderDataType::Float2: return 4 * 2;
		case ShaderDataType::Float3: return 4 * 3;
		case ShaderDataType::Float4: return 4 * 4;
		case ShaderDataType::Mat3:	 return 4 * 9;
		case ShaderDataType::Mat4:	 return 4 * 16;
		case ShaderDataType::Int:	 return 4;
		case ShaderDataType::Int2:	 return 4 * 2;
		case ShaderDataType::Int3:	 return 4 * 3;
		case ShaderDataType::Int4:	 return 4 * 4;
		case ShaderDataType::Bool:	 return 1;
		}
		CORE_ASSERT(false, "Unknown ShaderDataType!");
		return 0;
	}
	/////////////////////////////////////////////////////////////////////// BufferElement
	struct BufferElement
	{
		std::string Name;
		ShaderDataType Type;
		uint32_t Size;
		uint32_t Offset;
		bool Normalized;
		BufferElement() = default;
		BufferElement(const ShaderDataType type, const std::string& name, const bool normalized = false)
			: Name(name), Type(type), Size(0), Offset(0), Normalized(normalized)
		{
			Size = ShaderDataTypeSize(type);
		}


		uint32_t GetComponentCount() const
		{
			switch (Type)
			{
				case ShaderDataType::Float:   return 1;
				case ShaderDataType::Float2:  return 2;
				case ShaderDataType::Float3:  return 3;
				case ShaderDataType::Float4:  return 4;
				case ShaderDataType::Mat3:    return 9;		// 3* float3
				case ShaderDataType::Mat4:    return 16;	// 4* float4
				case ShaderDataType::Int:     return 1;
				case ShaderDataType::Int2:    return 2;
				case ShaderDataType::Int3:    return 3;
				case ShaderDataType::Int4:    return 4;
				case ShaderDataType::Bool:    return 1;
			}


			CORE_ASSERT(false, "Unknown ShaderDataType!");
			return 0;
		}
	};
	/////////////////////////////////////////////////////////////////////// BufferLayout
	class VertexBufferLayout
	{
	public:
		VertexBufferLayout() = default;
		VertexBufferLayout(const std::initializer_list<BufferElement>& elements)
			: m_Elements(elements)
		{
			CalculateOffsetAndStride();
		}
		inline const std::vector<BufferElement>& GetElements() const { return m_Elements; }
		inline const uint32_t GetStride() const { return m_Stride; }

		/// ITERATORS
		std::vector<BufferElement>::iterator begin() { return m_Elements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elements.end(); }
		std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
		std::vector<BufferElement>::const_iterator end() const { return m_Elements.end(); }
	private:
		std::vector<BufferElement> m_Elements;
		uint32_t m_Stride = 0;
	private:
		void CalculateOffsetAndStride()
		{
			uint32_t offset = 0;
			m_Stride = 0;
			for (auto& element : m_Elements)
			{
				element.Offset = offset;
				offset += element.Size;
				m_Stride += element.Size;
			}
		}
	};


	class VertexBuffer
	{
	public:
		VertexBuffer(const void* data, uint32_t size);
		VertexBuffer(uint32_t size);
		virtual ~VertexBuffer();

		inline VkBuffer GetVulkanBuffer() const { return m_VertexBuffer; }
		inline uint32_t GetSize() const { return m_Size; }
		const VertexBufferLayout& GetLayout() const { return m_VertexBufferLayout; }
		void SetVertexBufferLayout(const VertexBufferLayout& layout) { m_VertexBufferLayout = layout; }

	private:
		VkBuffer m_VertexBuffer;
		uint32_t m_Size;
		Buffer m_LocalData;
		VmaAllocation m_MemoryAllocation;
		VertexBufferLayout m_VertexBufferLayout;

	};
}
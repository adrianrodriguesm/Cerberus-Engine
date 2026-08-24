#pragma once
#include "Renderer/Memory/ResourceAllocator.h"
#include "Core/Buffer.h"
#include "Renderer/RHI/Buffers/GPUBuffer.h"
#include "Core/Handle.h"
namespace cerberus
{

	class UniformBuffer
	{
	public:
		UniformBuffer(uint32_t size, uint32_t binding, BufferUsageType bufferUsage = BufferUsageType::Static, uint64_t aligment = 0);
		~UniformBuffer();

		void SetData(void* data, uint32_t size, uint32_t offset);
		uint32_t GetBindingPoint() const { return m_BindingPoint; }
		uint64_t GetAligmentData() const { return m_AligmentData; }
		BufferUsageType GetBufferUsageType() const { return m_BufferUsageType; }
		const VkDescriptorBufferInfo& GetBufferDescriptorInfo() const { return m_BufferDescriptorInfo; }
	private:
		void Release();
		void Invalidate();
	private:
		VkDescriptorBufferInfo m_BufferDescriptorInfo;
		Handle<GPUBuffer> m_UniformBuffer;
		uint32_t m_BindingPoint;
		Buffer m_LocalData;
		uint32_t m_Size;
		BufferUsageType m_BufferUsageType;
		uint64_t m_AligmentData;
	};
};
#pragma once
#include "Renderer/StorageBuffer.h"
namespace cerberus
{
	struct StorageBufferSetSpecification
	{
		uint32_t Size;
		uint32_t BindingPoint;
		BufferUsageType BufferUsage = BufferUsageType::Static;
		uint64_t Aligment = 0;
		uint32_t Frames = 3;
	};

	class StorageBufferSet
	{
	public:
		StorageBufferSet(const StorageBufferSetSpecification& spec)
			: m_Spec(spec)
		{
			for (uint32_t frame = 0; frame < m_Spec.Frames; frame++)
			{
				Ref<StorageBuffer> storageBuffer = CreateRef<StorageBuffer>(m_Spec.Size, m_Spec.BindingPoint, m_Spec.BufferUsage, m_Spec.Aligment);
				m_StorageBuffers.push_back(storageBuffer);
			}
		}
		virtual ~StorageBufferSet() {}

		Ref<StorageBuffer> Get(uint32_t frame = 0)
		{
			CORE_ASSERT(frame < m_Spec.Frames, "Frame number does not exits");
			return m_StorageBuffers[frame];
		}

	private:
		StorageBufferSetSpecification m_Spec;
		std::vector<Ref<StorageBuffer>> m_StorageBuffers;
	};
}
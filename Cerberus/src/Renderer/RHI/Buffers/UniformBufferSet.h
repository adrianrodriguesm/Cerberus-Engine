#pragma once
#include "Renderer/RHI/Buffers/UniformBuffer.h"
namespace cerberus
{
	struct UniformBufferSetSpecification
	{
		uint32_t Size;
		uint32_t BindingPoint;
		BufferUsageType BufferUsage = BufferUsageType::Static;
		uint64_t Aligment = 0;
		uint32_t Frames = 3;
	};

	class UniformBufferSet
	{
	public:
		UniformBufferSet(const UniformBufferSetSpecification& spec)
			: m_Spec(spec)
		{
			for (uint32_t frame = 0; frame < m_Spec.Frames; frame++)
			{
				Ref<UniformBuffer> uniformBuffer = CreateRef<UniformBuffer>(m_Spec.Size, m_Spec.BindingPoint, m_Spec.BufferUsage, m_Spec.Aligment);
				m_UniformBuffers.push_back(uniformBuffer);
			}
		}
		virtual ~UniformBufferSet() {}

		Ref<UniformBuffer> Get(uint32_t frame = 0)
		{
			CORE_ASSERT(frame < m_Spec.Frames, "Frame number does not exits");
			return m_UniformBuffers[frame];
		}

	private:
		UniformBufferSetSpecification m_Spec;
		std::vector<Ref<UniformBuffer>> m_UniformBuffers; 
	};
}
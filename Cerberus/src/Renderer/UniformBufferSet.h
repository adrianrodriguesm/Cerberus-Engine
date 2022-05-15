#pragma once

#include "Renderer/UniformBuffer.h"
#include <map>
namespace cerberus
{
	class UniformBufferSet
	{
	public:
		UniformBufferSet(uint32_t frames)
			: m_Frames(frames) {}
		virtual ~UniformBufferSet() {}

		void Create(uint32_t size, uint32_t binding, BufferUsageType bufferUsage = BufferUsageType::Static, uint64_t aligment = 0)
		{
			for (uint32_t frame = 0; frame < m_Frames; frame++)
			{
				Ref<UniformBuffer> uniformBuffer = CreateRef<UniformBuffer>(size, binding, bufferUsage, aligment);
				Set(uniformBuffer, 0, frame);
			}
		}

		Ref<UniformBuffer> Get(uint32_t binding, uint32_t set = 0, uint32_t frame = 0)
		{
			CORE_ASSERT(m_UniformBuffers.find(frame) != m_UniformBuffers.end(), "Frame number does not exits");
			CORE_ASSERT(m_UniformBuffers.at(frame).find(set) != m_UniformBuffers.at(frame).end(), "Set at that frame does not exit");
			CORE_ASSERT(m_UniformBuffers.at(frame).at(set).find(binding) != m_UniformBuffers.at(frame).at(set).end(), "Binding point does not exit");

			return m_UniformBuffers.at(frame).at(set).at(binding);
		}

		void Set(Ref<UniformBuffer> uniformBuffer, uint32_t set = 0, uint32_t frame = 0)
		{
			m_UniformBuffers[frame][set][uniformBuffer->GetBindingPoint()] = uniformBuffer;
		}
	private:
		uint32_t m_Frames;
		// Frame->Set->Binding
		std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, Ref<UniformBuffer>>>> m_UniformBuffers; 
	};
}


#include "pch.h"
#include "TransientCommandBuffer.h"
#include "Renderer/TransientCommandExecutor.h"

namespace cerberus
{
	TransientCommandBuffer::TransientCommandBuffer(
		VkCommandBuffer commandBuffer,
		CommandPool* commandPool,
		TransientCommandExecutor* executor)
		: m_CommandBuffer(commandBuffer), m_CommandPool(commandPool), m_Executor(executor)
	{
	}

	TransientCommandBuffer::~TransientCommandBuffer()
	{
		Free();
	}

	TransientCommandBuffer::TransientCommandBuffer(TransientCommandBuffer&& other) noexcept
	{
		MoveFrom(std::move(other));
	}

	TransientCommandBuffer& TransientCommandBuffer::operator=(TransientCommandBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Free();
			MoveFrom(std::move(other));
		}

		return *this;
	}

	void TransientCommandBuffer::SubmitAndWait()
	{
		CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "Cannot submit an invalid transient command buffer");
		CORE_ASSERT(m_CommandPool, "Transient command buffer is missing its owning CommandPool");
		CORE_ASSERT(m_Executor, "Transient command buffer is missing its executor");
		m_Executor->SubmitAndWait(*this);
	}

	void TransientCommandBuffer::Free()
	{
		if (m_CommandBuffer == VK_NULL_HANDLE)
			return;

		CORE_ASSERT(m_CommandPool, "Transient command buffer is missing its owning CommandPool");
		CORE_ASSERT(m_Executor, "Transient command buffer is missing its executor");
		m_Executor->Recycle(*this);
	}

	void TransientCommandBuffer::MoveFrom(TransientCommandBuffer&& other) noexcept
	{
		m_CommandBuffer = other.m_CommandBuffer;
		m_CommandPool = other.m_CommandPool;
		m_Executor = other.m_Executor;

		other.m_CommandBuffer = VK_NULL_HANDLE;
		other.m_CommandPool = nullptr;
		other.m_Executor = nullptr;
	}
}

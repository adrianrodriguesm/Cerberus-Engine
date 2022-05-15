#include "pch.h"
#include "RenderQueue.h"

namespace cerberus
{
	RenderQueue::RenderQueue()
		: m_CommandCount(0)
	{
		m_CommandBuffer = new uint8_t[10 * 1024 * 1024]; // 10 MB
		m_CommandPtr = m_CommandBuffer;
		memset(m_CommandBuffer, 0, 10 * 1024 * 1024);
	}
	RenderQueue::~RenderQueue()
	{
		delete[] m_CommandBuffer;
	}

	void* RenderQueue::Allocate(RenderCommandFn function, uint32_t size)
	{
		// Store the Render Command
		*(RenderCommandFn*)m_CommandPtr = function;
		m_CommandPtr += sizeof(RenderCommandFn);
		// Store the size of the function that will be executed by the render command
		*(uint32_t*)m_CommandPtr = size;
		m_CommandPtr += sizeof(uint32_t);
		// Point to the memory of the function
		void* functionMemory = m_CommandPtr;
		// Store the function (empty) with their respective size
		m_CommandPtr += size;
		// Update the number of render command to execute
		m_CommandCount++;
		// Return the function memory
		return functionMemory;
	}

	void RenderQueue::Execute()
	{
		if (m_CommandCount == 0)
			return;
		// Point to the beggining of the buffer
		uint8_t* commandBuffer = m_CommandBuffer;
		for (uint32_t i = 0; i < m_CommandCount; i++)
		{
			// Get the Render Command Function
			RenderCommandFn renderCommandFn = *(RenderCommandFn*)commandBuffer;
			// Move the pointer and get function size
			commandBuffer += sizeof(RenderCommandFn);
			uint32_t functionSize = *(uint32_t*)commandBuffer;
			// Move the pointer to the beginning of the function 
			commandBuffer += sizeof(uint32_t);
			// Execute the render command
			renderCommandFn(commandBuffer);
			// Move the pointer in the buffer to the end of the function
			commandBuffer += functionSize;
		}
		// Reset
		m_CommandPtr = m_CommandBuffer;
		m_CommandCount = 0;
	}


}
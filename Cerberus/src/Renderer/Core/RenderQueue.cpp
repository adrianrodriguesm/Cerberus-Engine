#include "pch.h"
#include "RenderQueue.h"

namespace cerberus
{
	static constexpr size_t s_CommandBufferSize = 10 * 1024 * 1024; // 10 MB

	RenderQueue::RenderQueue()
		: m_WriteOffset(0)
		, m_ReadOffset(0)
		, m_ExecuteDepth(0)
	{
		m_CommandBuffer = new uint8_t[s_CommandBufferSize];
		memset(m_CommandBuffer, 0, s_CommandBufferSize);
	}
	RenderQueue::~RenderQueue()
	{
		delete[] m_CommandBuffer;
	}

	void* RenderQueue::Allocate(RenderCommandFn function, uint32_t size)
	{
		const size_t entrySize = sizeof(RenderCommandFn) + sizeof(uint32_t) + size;
		CORE_ASSERT(m_WriteOffset + entrySize <= s_CommandBufferSize, "RenderQueue command buffer is full");

		uint8_t* commandPtr = m_CommandBuffer + m_WriteOffset;
		// Store the Render Command
		*(RenderCommandFn*)commandPtr = function;
		commandPtr += sizeof(RenderCommandFn);
		// Store the size of the function that will be executed by the render command
		*(uint32_t*)commandPtr = size;
		commandPtr += sizeof(uint32_t);
		// Point to the memory of the function
		void* functionMemory = commandPtr;
		// Reserve the function's (empty) storage
		m_WriteOffset += entrySize;
		return functionMemory;
	}

	void RenderQueue::Execute()
	{
		// Depth-tracked and offset-based (not "walk from the start, then
		// reset to the start") specifically so this is safe to call again
		// from inside a command this same call is currently running --
		// e.g. EnvironmentMapBaker::Bake() forcing its own just-submitted
		// ComputePipeline::Invalidate() to run immediately instead of
		// waiting for the frame's normal flush, when Bake() itself was
		// reached from inside an already-executing queued command (a
		// SkyLightComponent's EnvironmentMap resolving lazily mid-draw,
		// while the whole ImGui frame is itself one such command). The
		// previous implementation always restarted from the buffer's start
		// and reset it to empty when done, so a nested call while an outer
		// one was mid-walk re-ran already-executed commands and then wiped
		// out the outer call's remaining ones out from under it.
		m_ExecuteDepth++;

		while (m_ReadOffset < m_WriteOffset)
		{
			uint8_t* commandPtr = m_CommandBuffer + m_ReadOffset;
			// Get the Render Command Function
			RenderCommandFn renderCommandFn = *(RenderCommandFn*)commandPtr;
			commandPtr += sizeof(RenderCommandFn);
			uint32_t functionSize = *(uint32_t*)commandPtr;
			commandPtr += sizeof(uint32_t);
			// Advance the shared read cursor before invoking the command --
			// not after -- so a command that recursively calls Execute()
			// (from Submit()'d work of its own) sees this one as already
			// consumed and starts reading from the next one, rather than
			// re-reading this same entry's still-live bytes as a fresh
			// command header.
			m_ReadOffset += sizeof(RenderCommandFn) + sizeof(uint32_t) + functionSize;
			// Execute the render command
			renderCommandFn(commandPtr);
		}

		m_ExecuteDepth--;

		// Only the outermost call may rewind the buffer, and only once the
		// queue is confirmed empty -- re-checked here rather than assumed,
		// since a nested call made from the last command this loop ran could
		// have appended more after that command returned but before this
		// point.
		if (m_ExecuteDepth == 0 && m_ReadOffset == m_WriteOffset)
		{
			m_ReadOffset = 0;
			m_WriteOffset = 0;
		}
	}
}

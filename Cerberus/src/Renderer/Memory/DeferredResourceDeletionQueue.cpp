#include "pch.h"
#include "DeferredResourceDeletionQueue.h"

namespace cerberus
{
	struct DeferredResourceDeletionQueueData
	{
		uint32_t FramesInFlight = 0;
		bool ShuttingDown = false;
		std::vector<RenderQueue*> Queues;
	};

	static DeferredResourceDeletionQueueData* s_DeferredResourceDeletionQueueData = nullptr;

	void DeferredResourceDeletionQueue::Init(uint32_t framesInFlight)
	{
		CORE_ASSERT(framesInFlight > 0, "DeferredResourceDeletionQueue requires at least one frame in flight");
		CORE_ASSERT(!s_DeferredResourceDeletionQueueData, "DeferredResourceDeletionQueue already initialized");

		s_DeferredResourceDeletionQueueData = new DeferredResourceDeletionQueueData();
		s_DeferredResourceDeletionQueueData->FramesInFlight = framesInFlight;
		s_DeferredResourceDeletionQueueData->Queues.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; i++)
			s_DeferredResourceDeletionQueueData->Queues[i] = new RenderQueue();
	}

	void DeferredResourceDeletionQueue::Shutdown()
	{
		if (!s_DeferredResourceDeletionQueueData)
			return;

		// Renderer::Shutdown() is responsible for calling FlushAll() while Vulkan,
		// ResourceManager and ResourceAllocator are still alive. Do not Execute()
		// queued deletions here; at this stage the backing systems may already be gone.
		s_DeferredResourceDeletionQueueData->ShuttingDown = true;

		for (RenderQueue*& queue : s_DeferredResourceDeletionQueueData->Queues)
		{
			delete queue;
			queue = nullptr;
		}

		delete s_DeferredResourceDeletionQueueData;
		s_DeferredResourceDeletionQueueData = nullptr;
	}

	void DeferredResourceDeletionQueue::FlushFrame(uint32_t frameIndex)
	{
		if (!s_DeferredResourceDeletionQueueData)
			return;

		GetQueue(frameIndex).Execute();
	}

	void DeferredResourceDeletionQueue::FlushAll()
	{
		if (!s_DeferredResourceDeletionQueueData)
			return;

		for (RenderQueue* queue : s_DeferredResourceDeletionQueueData->Queues)
		{
			if (queue)
				queue->Execute();
		}
	}

	bool DeferredResourceDeletionQueue::IsInitialized()
	{
		return s_DeferredResourceDeletionQueueData != nullptr;
	}

	bool DeferredResourceDeletionQueue::IsShuttingDown()
	{
		return s_DeferredResourceDeletionQueueData && s_DeferredResourceDeletionQueueData->ShuttingDown;
	}

	RenderQueue& DeferredResourceDeletionQueue::GetQueue(uint32_t frameIndex)
	{
		CORE_ASSERT(s_DeferredResourceDeletionQueueData, "DeferredResourceDeletionQueue is not initialized");
		CORE_ASSERT(frameIndex < s_DeferredResourceDeletionQueueData->FramesInFlight, "Deferred resource deletion frame index out of bounds");
		CORE_ASSERT(s_DeferredResourceDeletionQueueData->Queues[frameIndex], "Invalid deferred resource deletion queue");
		return *s_DeferredResourceDeletionQueueData->Queues[frameIndex];
	}
}

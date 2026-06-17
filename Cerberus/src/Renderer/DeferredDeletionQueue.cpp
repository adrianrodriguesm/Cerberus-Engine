#include "pch.h"
#include "DeferredDeletionQueue.h"

namespace cerberus
{
	struct DeferredDeletionQueueData
	{
		uint32_t FramesInFlight = 0;
		bool ShuttingDown = false;
		std::vector<RenderQueue*> Queues;
	};

	static DeferredDeletionQueueData* s_DeferredDeletionQueueData = nullptr;

	void DeferredDeletionQueue::Init(uint32_t framesInFlight)
	{
		CORE_ASSERT(framesInFlight > 0, "DeferredDeletionQueue requires at least one frame in flight");
		CORE_ASSERT(!s_DeferredDeletionQueueData, "DeferredDeletionQueue already initialized");

		s_DeferredDeletionQueueData = new DeferredDeletionQueueData();
		s_DeferredDeletionQueueData->FramesInFlight = framesInFlight;
		s_DeferredDeletionQueueData->Queues.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; i++)
			s_DeferredDeletionQueueData->Queues[i] = new RenderQueue();
	}

	void DeferredDeletionQueue::Shutdown()
	{
		if (!s_DeferredDeletionQueueData)
			return;

		// Renderer::Shutdown() is responsible for calling FlushAll() while Vulkan,
		// ResourceManager and ResourceAllocator are still alive. Do not Execute()
		// queued deletions here; at this stage the backing systems may already be gone.
		s_DeferredDeletionQueueData->ShuttingDown = true;

		for (RenderQueue*& queue : s_DeferredDeletionQueueData->Queues)
		{
			delete queue;
			queue = nullptr;
		}

		delete s_DeferredDeletionQueueData;
		s_DeferredDeletionQueueData = nullptr;
	}

	void DeferredDeletionQueue::FlushFrame(uint32_t frameIndex)
	{
		if (!s_DeferredDeletionQueueData)
			return;

		GetQueue(frameIndex).Execute();
	}

	void DeferredDeletionQueue::FlushAll()
	{
		if (!s_DeferredDeletionQueueData)
			return;

		for (RenderQueue* queue : s_DeferredDeletionQueueData->Queues)
		{
			if (queue)
				queue->Execute();
		}
	}

	bool DeferredDeletionQueue::IsInitialized()
	{
		return s_DeferredDeletionQueueData != nullptr;
	}

	bool DeferredDeletionQueue::IsShuttingDown()
	{
		return s_DeferredDeletionQueueData && s_DeferredDeletionQueueData->ShuttingDown;
	}

	RenderQueue& DeferredDeletionQueue::GetQueue(uint32_t frameIndex)
	{
		CORE_ASSERT(s_DeferredDeletionQueueData, "DeferredDeletionQueue is not initialized");
		CORE_ASSERT(frameIndex < s_DeferredDeletionQueueData->FramesInFlight, "Deferred deletion frame index out of bounds");
		CORE_ASSERT(s_DeferredDeletionQueueData->Queues[frameIndex], "Invalid deferred deletion queue");
		return *s_DeferredDeletionQueueData->Queues[frameIndex];
	}
}

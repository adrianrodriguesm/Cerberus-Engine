#include "pch.h"
#include "RenderQueueManager.h"

namespace cerberus
{
	struct RenderCoreData
	{
		RenderQueue* CommandQueue = nullptr;
	};

	static RenderCoreData* s_RenderCoreData = nullptr;

	void RenderQueueManager::Init()
	{
		CORE_ASSERT(!s_RenderCoreData, "RenderQueueManager already initialized");
		s_RenderCoreData = new RenderCoreData();
		s_RenderCoreData->CommandQueue = new RenderQueue();
	}

	void RenderQueueManager::Shutdown()
	{
		if (!s_RenderCoreData)
			return;

		// Renderer::Shutdown() must already have drained the command queue.
		// Do not Execute() here: render commands may submit deferred deletions,
		// and the deletion/resource systems may already be shutting down.
		delete s_RenderCoreData->CommandQueue;
		s_RenderCoreData->CommandQueue = nullptr;

		delete s_RenderCoreData;
		s_RenderCoreData = nullptr;
	}

	void RenderQueueManager::FreeAllSubmittedResources()
	{
		DeferredDeletionQueue::FlushAll();
	}

	void RenderQueueManager::FreeResourceFromQueue(const uint32_t frameIndex)
	{
		DeferredDeletionQueue::FlushFrame(frameIndex);
	}

	void RenderQueueManager::ExecuteRenderQueue()
	{
		if (!s_RenderCoreData || !s_RenderCoreData->CommandQueue)
			return;

		s_RenderCoreData->CommandQueue->Execute();
	}

	RenderQueue& RenderQueueManager::GetRenderCommandQueue()
	{
		CORE_ASSERT(s_RenderCoreData, "RenderQueueManager is not initialized");
		CORE_ASSERT(s_RenderCoreData->CommandQueue, "Invalid render command queue");
		return *s_RenderCoreData->CommandQueue;
	}
}

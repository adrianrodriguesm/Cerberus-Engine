#include "pch.h"
#include "RenderQueueManager.h"


namespace cerberus
{
	struct RenderCoreData
	{
		RenderQueue* CommandQueue = nullptr;
		std::array<RenderQueue*, 3> ResourceFreeQueue;
	};

	static RenderCoreData* s_RenderCoreData;

	void RenderQueueManager::Init()
	{
		s_RenderCoreData = new RenderCoreData();
		s_RenderCoreData->CommandQueue = new RenderQueue();

		for (uint32_t i = 0; i < 3; i++)
		{
			s_RenderCoreData->ResourceFreeQueue[i] = new RenderQueue();
		}
	}

	void RenderQueueManager::ShutDown()
	{
		for (uint32_t i = 0; i < 3; i++)
		{
			s_RenderCoreData->ResourceFreeQueue[i]->Execute();
			delete s_RenderCoreData->ResourceFreeQueue[i];
		}

		s_RenderCoreData->CommandQueue->Execute();
		delete s_RenderCoreData->CommandQueue;

		delete s_RenderCoreData;
	}

	void RenderQueueManager::FreeAllSubmittedResources()
	{
		for (uint32_t i = 0; i < 3; i++)
		{
			s_RenderCoreData->ResourceFreeQueue[i]->Execute();
		}
	}

	void RenderQueueManager::FreeResourceFromQueue(const uint32_t frameIndex)
	{
		CORE_ASSERT(frameIndex < Renderer::GetRendererConfig().FramesInFlight, "Frame index out of bounds!");
		s_RenderCoreData->ResourceFreeQueue[frameIndex]->Execute();
	}

	void RenderQueueManager::ExecuteRenderQueue()
	{
		s_RenderCoreData->CommandQueue->Execute();
	}

	RenderQueue& RenderQueueManager::GetRenderCommandQueue()
	{
		return *s_RenderCoreData->CommandQueue;
	}

	RenderQueue& RenderQueueManager::GetRenderResourceReleaseQueue(uint32_t index)
	{
		return *s_RenderCoreData->ResourceFreeQueue[index];
	}

}
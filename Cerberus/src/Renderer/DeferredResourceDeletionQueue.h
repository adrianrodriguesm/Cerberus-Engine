#pragma once

#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
#include <type_traits>

namespace cerberus
{
	class DeferredResourceDeletionQueue
	{
	public:
		static void Init(uint32_t framesInFlight);
		static void Shutdown();

		template<typename FuncT>
		static void SubmitForFrame(uint32_t frameIndex, FuncT&& func)
		{
			using CommandT = std::decay_t<FuncT>;

			CORE_ASSERT(IsInitialized(), "DeferredResourceDeletionQueue is not initialized");
			CORE_ASSERT(!IsShuttingDown(), "Cannot submit deferred deletions while DeferredResourceDeletionQueue is shutting down");

			auto resourceFreeCommand = [](void* ptr)
			{
				auto pFunction = reinterpret_cast<CommandT*>(ptr);
				(*pFunction)();
				pFunction->~CommandT();
			};

			RenderQueue& queue = GetQueue(frameIndex);
			void* functionMemoryAllocation = queue.Allocate(resourceFreeCommand, sizeof(CommandT));
			new (functionMemoryAllocation) CommandT(std::forward<FuncT>(func));
		}

		template<typename FuncT>
		static void SubmitCurrentFrame(FuncT&& func)
		{
			const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			SubmitForFrame(frameIndex, std::forward<FuncT>(func));
		}

		static void FlushFrame(uint32_t frameIndex);
		static void FlushAll();

		static bool IsInitialized();
		static bool IsShuttingDown();

	private:
		static RenderQueue& GetQueue(uint32_t frameIndex);
	};
}

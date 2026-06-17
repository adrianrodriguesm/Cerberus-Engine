#pragma once
#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
#include "Renderer/DeferredDeletionQueue.h"
#include <type_traits>

namespace cerberus
{
	class RenderQueueManager
	{
	public:
		static void Init();
		static void Shutdown();

		// Submit a render command to the render command queue.
		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			using CommandT = std::decay_t<FuncT>;

			auto renderCommand = [](void* ptr)
			{
				auto pFunction = reinterpret_cast<CommandT*>(ptr);
				(*pFunction)();
				pFunction->~CommandT();
			};

			void* functionMemoryAllocation = GetRenderCommandQueue().Allocate(renderCommand, sizeof(CommandT));
			new (functionMemoryAllocation) CommandT(std::forward<FuncT>(func));
		}

		// Compatibility wrapper. Resource lifetime now belongs to DeferredDeletionQueue,
		// not to the render command queue.
		template<typename FuncT>
		static void SubmitResourceFree(FuncT&& func)
		{
			const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
			DeferredDeletionQueue::Submit(frameIndex, std::forward<FuncT>(func));
		}

		// Compatibility wrappers kept to avoid touching all call sites in phase 1.
		static void FreeAllSubmittedResources();
		static void FreeResourceFromQueue(const uint32_t frameIndex);
		static void ExecuteRenderQueue();

	private:
		static RenderQueue& GetRenderCommandQueue();
	};
}

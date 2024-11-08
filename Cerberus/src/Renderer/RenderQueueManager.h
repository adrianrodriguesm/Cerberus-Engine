#pragma once
#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
namespace cerberus
{
	class RenderQueueManager
	{
	public:
		static void Init();
		static void ShutDown();

		// Submit Command to the RenderQueue
		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			auto renderCommand = [](void* ptr)
			{
				auto pFunction = (FuncT*)ptr;
				// Execute Function
				(*pFunction)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunction->~FuncT();
			};
			auto functionMemoryAllocation = GetRenderCommandQueue().Allocate(renderCommand, sizeof(func));
			// Construct function (emplace new)
			new (functionMemoryAllocation) FuncT(std::forward<FuncT>(func));
		}

		template<typename FuncT>
		static void SubmitResourceFree(FuncT&& func)
		{
			auto renderCommand = [](void* ptr)
			{
				auto pFunction = (FuncT*)ptr;
				// Execute Function
				(*pFunction)();

				// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
				// however some items like uniforms which contain std::strings still exist for now
				// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
				pFunction->~FuncT();
			};

			Submit([renderCommand, func]()
				{
					const uint32_t index = Renderer::GetCurrentFrameIndex();
					auto functionMemoryAllocation = GetRenderResourceReleaseQueue(index).Allocate(renderCommand, sizeof(func));
					// Construct function (emplace new)
					new (functionMemoryAllocation) FuncT(std::forward<FuncT>((FuncT&&)func));
				});
		}


		static void FreeAllSubmittedResources();
		static void FreeResourceFromQueue(const uint32_t frameIndex);
		static void ExecuteRenderQueue();
	private:
		static RenderQueue& GetRenderCommandQueue();
		static RenderQueue& GetRenderResourceReleaseQueue(uint32_t index);
	};
}
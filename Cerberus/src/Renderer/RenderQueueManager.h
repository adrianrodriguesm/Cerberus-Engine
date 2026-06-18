#pragma once
#include "Renderer/RenderQueue.h"
#include "Renderer/Renderer.h"
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


		static void ExecuteRenderQueue();

	private:
		static RenderQueue& GetRenderCommandQueue();
	};
}

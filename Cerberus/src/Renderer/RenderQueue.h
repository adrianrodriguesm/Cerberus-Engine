#pragma once

namespace cerberus
{
	
	typedef void (*RenderCommandFn) (void*);
	
	class RenderQueue
	{
	public:
		RenderQueue();
		~RenderQueue();
		void* Allocate(RenderCommandFn function, uint32_t size);
		void Execute();

	private:
		uint32_t m_CommandCount;
		uint8_t* m_CommandBuffer;
		uint8_t* m_CommandPtr;
	};
}
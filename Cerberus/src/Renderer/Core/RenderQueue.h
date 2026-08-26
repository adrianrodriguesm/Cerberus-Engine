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
		// Reentrant: safe to call again from inside a command that Execute()
		// is currently running (e.g. GPU work that must complete immediately,
		// such as a compute bake, forcing its own just-submitted pipeline
		// creation to run right away instead of waiting for the normal
		// once-per-frame flush). See the .cpp for why that requires a
		// persistent read cursor and depth tracking rather than the
		// once-through, always-rewind-to-the-start walk this used to do.
		void Execute();

	private:
		uint8_t* m_CommandBuffer;
		// Where the next Allocate() will write. Only ever rewound (back to 0,
		// alongside m_ReadOffset) once Execute() has confirmed nothing --
		// including anything a nested call appended -- is left unread.
		size_t m_WriteOffset;
		// Where Execute() will next read from. A persistent member, not a
		// local variable, so a nested Execute() call shares the exact same
		// position as whichever outer call is already walking the buffer:
		// it only ever advances past commands neither has run yet, never
		// re-running one the outer call already started, and never resetting
		// the buffer out from under it.
		size_t m_ReadOffset;
		// >0 while any Execute() call (nested or not) is on the call stack.
		// Only the outermost call is allowed to rewind the buffer.
		int m_ExecuteDepth;
	};
}

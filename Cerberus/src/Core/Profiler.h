#pragma once

#include "Core/Timer.h"

#include <span>
#include <vector>

namespace cerberus
{
	class CommandBuffer;

	struct CPUProfileEntry
	{
		// Every caller passes a string literal or __FUNCTION__ (static storage
		// duration), so this stores the pointer directly rather than copying
		// into an owned std::string -- one fewer allocation per scope per
		// frame while profiling is on.
		const char* Name = "";
		float Milliseconds = 0.0f;
		// A scope's children are exactly the ProfileScopes constructed and
		// destroyed while it was the innermost active one -- a real tree, not
		// a depth number. Two sibling scopes (e.g. one after another with no
		// scope enclosing both) can't be confused with parent/child this way,
		// which a flat list ordered by destruction time and annotated with
		// only a depth count could not tell apart.
		std::vector<CPUProfileEntry> Children;
	};

	// Lightweight CPU+GPU instrumentation with a single runtime on/off switch.
	// When disabled, ProfileScope skips all work beyond one bool check, and
	// RenderGraph::Execute() skips its GPU timestamp queries entirely -- the
	// switch actually removes the cost, not just hides the numbers.
	//
	// This is intentionally a per-frame call tree with durations, not a
	// hierarchical flamegraph or timeline view across frames -- there's no
	// frame track yet, just the current frame's scope durations.
	class Profiler
	{
	public:
		static bool IsEnabled();
		static void SetEnabled(bool enabled);

		// Clears last frame's CPU entries. Call once per frame, before any
		// ProfileScope for the new frame is constructed (Renderer::BeginFrame).
		static void BeginFrame();

		// Root-level scopes (no enclosing ProfileScope) from the last fully
		// completed frame; each may have nested Children. One frame of lag --
		// see BeginFrame()'s doc comment in Profiler.cpp for why.
		static std::span<const CPUProfileEntry> GetCPUEntries();

		// Draws the "Profiler" ImGui section: the enable checkbox, the CPU
		// call tree, and -- if mainCommandBuffer is non-null -- a "Frame
		// Total" row read from its whole-recording GPU timestamp (query index
		// 0/1, already captured by CommandBuffer whenever profiling is on).
		static void OnImGuiRender(CommandBuffer* mainCommandBuffer);

	private:
		friend class ProfileScope;
		// Called by ProfileScope's constructor/destructor. Push opens a new
		// (initially empty) child list for the scope now becoming innermost;
		// Pop closes it, building that scope's entry (with whatever was
		// pushed into its child list in between) and appending it to whichever
		// list is now innermost -- its parent's children, or the frame's root
		// list if none.
		static void PushScope();
		static void PopScope(const char* name, float milliseconds);
	};

	// RAII CPU scope timer. Construction checks Profiler::IsEnabled() once;
	// if disabled, the destructor does no further work.
	class ProfileScope
	{
	public:
		explicit ProfileScope(const char* name);
		~ProfileScope();

		ProfileScope(const ProfileScope&) = delete;
		ProfileScope& operator=(const ProfileScope&) = delete;

	private:
		const char* m_Name;
		Timer m_Timer;
		bool m_Active;
	};
}

#define CORE_PROFILE_CONCAT_INNER(a, b) a##b
#define CORE_PROFILE_CONCAT(a, b) CORE_PROFILE_CONCAT_INNER(a, b)
// Times the enclosing scope under the given display name.
#define CORE_PROFILE_SCOPE(name) ::cerberus::ProfileScope CORE_PROFILE_CONCAT(profileScope_, __LINE__)(name)
// Times the enclosing function, named after it.
#define CORE_PROFILE_FUNCTION() CORE_PROFILE_SCOPE(__FUNCTION__)

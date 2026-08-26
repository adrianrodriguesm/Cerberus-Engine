#include "pch.h"
#include "Profiler.h"

#include "Renderer/RHI/Command/CommandBuffer.h"

#include <imgui/imgui.h>

namespace cerberus
{
	namespace
	{
		bool s_Enabled = true;
		// Root-level scopes for the frame currently being recorded.
		std::vector<CPUProfileEntry> s_RootEntries;
		// Root-level scopes from the last fully completed frame -- what
		// OnImGuiRender actually reads. Profiler::OnImGuiRender is drawn from
		// ImGuiRenderer::Render(), which runs before LayerUpdate/FrameSubmit/
		// Present in the same frame, so reading s_RootEntries directly would
		// only ever show whatever happened to run earlier in that same,
		// still-in-progress frame. Swapped in at the next BeginFrame(): one
		// frame of lag, but always the complete tree, the same tradeoff GPU
		// timestamp results already have.
		std::vector<CPUProfileEntry> s_DisplayEntries;
		// One entry per currently-active ProfileScope, each holding the
		// children collected for it so far. The scope that's about to close
		// is always s_ChildStack.back(); its parent (if any) is whichever
		// vector is now on top after popping it off.
		std::vector<std::vector<CPUProfileEntry>> s_ChildStack;
	}

	bool Profiler::IsEnabled()
	{
		return s_Enabled;
	}

	void Profiler::SetEnabled(bool enabled)
	{
		s_Enabled = enabled;
	}

	void Profiler::BeginFrame()
	{
		s_DisplayEntries.swap(s_RootEntries);
		s_RootEntries.clear();
		// Scopes always destruct before the frame that constructed them ends,
		// so this should already be empty; cleared defensively in case
		// profiling was toggled off mid-scope last frame (see ProfileScope).
		s_ChildStack.clear();
	}

	std::span<const CPUProfileEntry> Profiler::GetCPUEntries()
	{
		return s_DisplayEntries;
	}

	void Profiler::PushScope()
	{
		s_ChildStack.emplace_back();
	}

	void Profiler::PopScope(const char* name, float milliseconds)
	{
		CPUProfileEntry entry{ name, milliseconds, std::move(s_ChildStack.back()) };
		s_ChildStack.pop_back();

		std::vector<CPUProfileEntry>& target = s_ChildStack.empty() ? s_RootEntries : s_ChildStack.back();
		target.push_back(std::move(entry));
	}

	namespace
	{
		void DrawCPUEntries(std::span<const CPUProfileEntry> entries, float indent)
		{
			for (const CPUProfileEntry& entry : entries)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (indent > 0.0f)
					ImGui::Indent(indent);
				ImGui::TextUnformatted(entry.Name);
				if (indent > 0.0f)
					ImGui::Unindent(indent);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f ms", entry.Milliseconds);

				if (!entry.Children.empty())
					DrawCPUEntries(entry.Children, indent + 12.0f);
			}
		}
	}

	void Profiler::OnImGuiRender(CommandBuffer* mainCommandBuffer)
	{
		ImGui::Begin("Profiler");

		ImGui::Checkbox("Enable Profiling", &s_Enabled);

		if (!s_Enabled)
		{
			ImGui::TextDisabled("Off -- no CPU scopes are recorded and RenderGraph's GPU timestamps are skipped entirely.");
			ImGui::End();
			return;
		}

		ImGui::Spacing();
		if (mainCommandBuffer)
			ImGui::Text("Frame Total (GPU): %.3f ms", mainCommandBuffer->GetExecutionGPUTime(0));

		if (ImGui::BeginTable("ProfilerCPUEntries", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("CPU Scope");
			ImGui::TableSetupColumn("Time");
			ImGui::TableHeadersRow();

			DrawCPUEntries(s_DisplayEntries, 0.0f);

			ImGui::EndTable();
		}

		ImGui::End();
	}

	ProfileScope::ProfileScope(const char* name)
		: m_Name(name), m_Active(Profiler::IsEnabled())
	{
		if (m_Active)
			Profiler::PushScope();
	}

	ProfileScope::~ProfileScope()
	{
		if (!m_Active)
			return;

		Profiler::PopScope(m_Name, m_Timer.ElapsedMilliseconds());
	}
}

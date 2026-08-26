#include "pch.h"
#include "RenderGraph.h"

#include "Core/Profiler.h"
#include "ImGui/ImGuiUI.h"
#include "Renderer/RHI/Command/CommandBuffer.h"
#include "Renderer/RHI/Command/ComputeCommandList.h"
#include "Renderer/RHI/Command/GraphicsCommandList.h"
#include "Renderer/RHI/Command/RayTracingCommandList.h"
#include "Renderer/RHI/Image.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/RHI/Texture.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace cerberus
{
	namespace
	{
		// What one access resolves to on the GPU: the barrier's destination side
		// for this access, plus what it leaves visible for whichever access
		// (this frame) comes next.
		struct ResolvedAccess
		{
			VkAccessFlags DstAccess = 0;
			VkPipelineStageFlags DstStage = 0;
			VkImageLayout NewLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			RGAccessState Produced{};
		};

		ResolvedAccess ResolveAccess(const RGResourceAccess& access, bool isDepthFormat)
		{
			ResolvedAccess resolved;
			switch (access.Kind)
			{
			case RGAccessKind::ReadTexture:
				resolved.DstAccess = VK_ACCESS_SHADER_READ_BIT;
				resolved.DstStage = access.Stages;
				resolved.NewLayout = isDepthFormat
					? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
					: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				resolved.Produced = { VK_ACCESS_SHADER_READ_BIT, access.Stages };
				break;

			case RGAccessKind::WriteColorAttachment:
				resolved.DstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				resolved.DstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				resolved.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				resolved.Produced = { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
				break;

			case RGAccessKind::WriteDepthAttachment:
				resolved.DstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				resolved.DstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
				resolved.NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				// A depth write/test retires at LATE_FRAGMENT_TESTS regardless of
				// the EARLY_FRAGMENT_TESTS stage a barrier into it waits at.
				resolved.Produced = { VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
				break;

			case RGAccessKind::ReadWriteStorageImage:
				resolved.DstAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				resolved.DstStage = access.Stages;
				resolved.NewLayout = VK_IMAGE_LAYOUT_GENERAL;
				resolved.Produced = { VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, access.Stages };
				break;
			}
			return resolved;
		}

		Image* ResolveBackingImage(const RenderGraph::Resource& resource)
		{
			if (resource.IsImage)
				return ResourceManager::GetImage(resource.ImageHandle);

			RenderTargetTexture* target = ResourceManager::GetRenderTarget(resource.RenderTarget);
			CORE_ASSERT(target, "RenderGraph resource references an invalid render target");
			return ResourceManager::GetImage(target->GetImage());
		}

		VkImageSubresourceRange SubresourceRangeFor(bool isDepthFormat)
		{
			VkImageSubresourceRange range{};
			range.aspectMask = isDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			return range;
		}

		void InsertBarrier(
			const RenderGraph::Resource& resource,
			const RGResourceAccess& access,
			const CommandListBase& commandList)
		{
			const ResolvedAccess resolved = ResolveAccess(access, resource.IsDepthFormat);
			Image* image = ResolveBackingImage(resource);
			CORE_ASSERT(image, "RenderGraph resource has no backing image");

			image->InsertImageMemoryBarrier(
				commandList,
				access.Before.Access,
				resolved.DstAccess,
				resolved.NewLayout,
				access.Before.Stage,
				resolved.DstStage,
				SubresourceRangeFor(resource.IsDepthFormat));
		}

		const char* AccessKindName(RGAccessKind kind)
		{
			switch (kind)
			{
			case RGAccessKind::ReadTexture:            return "Read";
			case RGAccessKind::WriteColorAttachment:   return "WriteColor";
			case RGAccessKind::WriteDepthAttachment:   return "WriteDepth";
			case RGAccessKind::ReadWriteStorageImage:  return "ReadWriteStorage";
			}
			return "Unknown";
		}

		// Renders a Vk*Flags bitmask as the names of the bits RenderGraph.cpp
		// actually produces (see ResolveAccess/ImportRenderTarget/ImportImage),
		// falling back to raw hex for anything else so an unrecognized bit is
		// still visible rather than silently dropped.
		template<typename FlagsType>
		std::string DescribeFlags(FlagsType flags, std::span<const std::pair<FlagsType, const char*>> known)
		{
			if (flags == 0)
				return "None";

			std::string result;
			for (const auto& [bit, name] : known)
			{
				if (flags & bit)
				{
					if (!result.empty())
						result += "|";
					result += name;
					flags &= ~bit;
				}
			}
			if (flags != 0)
			{
				char buffer[24];
				snprintf(buffer, sizeof(buffer), "0x%x", static_cast<unsigned int>(flags));
				if (!result.empty())
					result += "|";
				result += buffer;
			}
			return result;
		}

		std::string DescribeAccessFlags(VkAccessFlags flags)
		{
			static constexpr std::pair<VkAccessFlags, const char*> known[] = {
				{ VK_ACCESS_SHADER_READ_BIT, "ShaderRead" },
				{ VK_ACCESS_SHADER_WRITE_BIT, "ShaderWrite" },
				{ VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, "ColorAttachmentWrite" },
				{ VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, "DepthStencilAttachmentWrite" },
			};
			return DescribeFlags<VkAccessFlags>(flags, known);
		}

		std::string DescribeStageFlags(VkPipelineStageFlags flags)
		{
			static constexpr std::pair<VkPipelineStageFlags, const char*> known[] = {
				{ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, "TopOfPipe" },
				{ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, "FragmentShader" },
				{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "ColorAttachmentOutput" },
				{ VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, "EarlyFragmentTests" },
				{ VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, "LateFragmentTests" },
				{ VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, "RayTracingShader" },
				{ VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, "ComputeShader" },
			};
			return DescribeFlags<VkPipelineStageFlags>(flags, known);
		}
	}

	void RenderGraphBuilder::ReadTexture(RGTextureHandle handle, VkPipelineStageFlags stages)
	{
		RGResourceAccess access;
		access.Resource = handle;
		access.Kind = RGAccessKind::ReadTexture;
		access.Stages = stages;
		m_Graph.RecordAccess(m_PassIndex, access);
	}

	void RenderGraphBuilder::WriteColorAttachment(RGTextureHandle handle, LOAD_OP loadOp, STORE_OP storeOp)
	{
		RGResourceAccess access;
		access.Resource = handle;
		access.Kind = RGAccessKind::WriteColorAttachment;
		access.Stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		access.LoadOp = loadOp;
		access.StoreOp = storeOp;
		m_Graph.RecordAccess(m_PassIndex, access);
	}

	void RenderGraphBuilder::WriteDepthAttachment(RGTextureHandle handle, LOAD_OP loadOp, STORE_OP storeOp)
	{
		RGResourceAccess access;
		access.Resource = handle;
		access.Kind = RGAccessKind::WriteDepthAttachment;
		access.Stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		access.LoadOp = loadOp;
		access.StoreOp = storeOp;
		m_Graph.RecordAccess(m_PassIndex, access);
	}

	void RenderGraphBuilder::ReadWriteStorageImage(RGTextureHandle handle, VkPipelineStageFlags stages)
	{
		RGResourceAccess access;
		access.Resource = handle;
		access.Kind = RGAccessKind::ReadWriteStorageImage;
		access.Stages = stages;
		m_Graph.RecordAccess(m_PassIndex, access);
	}

	RGTextureHandle RenderGraph::ImportRenderTarget(Handle<RenderTargetTexture> target, const char* name)
	{
		RenderTargetTexture* renderTarget = ResourceManager::GetRenderTarget(target);
		CORE_ASSERT(renderTarget, "RenderGraph::ImportRenderTarget received an invalid render target");

		Resource resource;
		resource.IsImage = false;
		resource.RenderTarget = target;
		resource.Name = name;
		resource.IsDepthFormat = renderTarget->IsDepthTarget();
		resource.IsShaderResource = renderTarget->IsShaderResource();
		// Matches the dual-purpose conservative barrier GraphicsCommandList::
		// BeginRendering used to hard-code for every call: the resource may
		// currently hold a previous frame's shader-read state, or -- before any
		// graph resource-history exists this frame -- another pass's attachment
		// write it is reused from via LOAD_OP::LOAD. Every access after the
		// first gets a barrier resolved from real per-frame history instead.
		resource.InitialState = resource.IsDepthFormat
			? RGAccessState{ VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT }
			: RGAccessState{ VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		RGTextureHandle handle{ static_cast<uint32_t>(m_Resources.size()) };
		m_Resources.push_back(std::move(resource));
		return handle;
	}

	RGTextureHandle RenderGraph::ImportImage(Handle<Image> image, const char* name, RGAccessState initialAccess)
	{
		CORE_ASSERT(ResourceManager::GetImage(image), "RenderGraph::ImportImage received an invalid image");

		Resource resource;
		resource.IsImage = true;
		resource.ImageHandle = image;
		resource.Name = name;
		resource.InitialState = initialAccess;

		RGTextureHandle handle{ static_cast<uint32_t>(m_Resources.size()) };
		m_Resources.push_back(std::move(resource));
		return handle;
	}

	void RenderGraph::RecordAccess(uint32_t passIndex, RGResourceAccess access)
	{
		CORE_ASSERT(access.Resource.IsValid() && access.Resource.Index < m_Resources.size(),
			"RenderGraph pass declared an access on an invalid/foreign resource handle");
		m_Passes[passIndex].Accesses.push_back(access);
	}

	void RenderGraph::AddGraphicsPass(const char* name, RGSetupFn setup, RGGraphicsExecuteFn execute)
	{
		CORE_ASSERT(!m_Compiled, "RenderGraph passes must be added before Compile()");
		Pass pass;
		pass.Name = name;
		pass.Type = Pass::Type::Graphics;
		pass.GraphicsExecute = std::move(execute);
		const uint32_t passIndex = static_cast<uint32_t>(m_Passes.size());
		m_Passes.push_back(std::move(pass));

		RenderGraphBuilder builder(*this, passIndex);
		setup(builder);
	}

	void RenderGraph::AddComputePass(const char* name, RGSetupFn setup, RGComputeExecuteFn execute)
	{
		CORE_ASSERT(!m_Compiled, "RenderGraph passes must be added before Compile()");
		Pass pass;
		pass.Name = name;
		pass.Type = Pass::Type::Compute;
		pass.ComputeExecute = std::move(execute);
		const uint32_t passIndex = static_cast<uint32_t>(m_Passes.size());
		m_Passes.push_back(std::move(pass));

		RenderGraphBuilder builder(*this, passIndex);
		setup(builder);
	}

	void RenderGraph::AddRayTracingPass(const char* name, RGSetupFn setup, RGRayTracingExecuteFn execute)
	{
		CORE_ASSERT(!m_Compiled, "RenderGraph passes must be added before Compile()");
		Pass pass;
		pass.Name = name;
		pass.Type = Pass::Type::RayTracing;
		pass.RayTracingExecute = std::move(execute);
		const uint32_t passIndex = static_cast<uint32_t>(m_Passes.size());
		m_Passes.push_back(std::move(pass));

		RenderGraphBuilder builder(*this, passIndex);
		setup(builder);
	}

	void RenderGraph::Compile()
	{
		CORE_ASSERT(!m_Compiled, "RenderGraph::Compile called twice without Reset()");

		const uint32_t passCount = static_cast<uint32_t>(m_Passes.size());

		// A pass depends on every earlier pass that wrote a resource it reads or
		// writes (RAW/WAW), and on every earlier reader of a resource it writes
		// (WAR). Cerberus's frame graph is small, so this straightforward scan
		// is fine; there is no need for a resource-indexed interval structure.
		std::vector<std::vector<uint32_t>> resourceWriters(m_Resources.size());
		std::vector<std::vector<uint32_t>> resourceReaders(m_Resources.size());

		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
		{
			Pass& pass = m_Passes[passIndex];
			for (const RGResourceAccess& access : pass.Accesses)
			{
				const uint32_t resourceIndex = access.Resource.Index;
				const bool isWrite = access.Kind != RGAccessKind::ReadTexture;

				for (uint32_t writer : resourceWriters[resourceIndex])
					if (writer != passIndex)
						pass.DependsOnPasses.push_back(writer);
				if (isWrite)
				{
					for (uint32_t reader : resourceReaders[resourceIndex])
						if (reader != passIndex)
							pass.DependsOnPasses.push_back(reader);
				}

				if (isWrite)
					resourceWriters[resourceIndex].push_back(passIndex);
				else
					resourceReaders[resourceIndex].push_back(passIndex);
			}
		}

		// A pass is a root if it writes anything -- which today always means
		// writing an imported (externally-owned) resource, since increment 1
		// has no graph-created transient resource a write could target
		// instead. A future transient resource would need its own default
		// here; until then, "writes anything" and "writes an imported
		// resource" are the same rule.
		std::vector<bool> kept(passCount, m_PassCullingEnabled ? false : true);
		if (m_PassCullingEnabled)
		{
			std::vector<uint32_t> toVisit;
			for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
			{
				for (const RGResourceAccess& access : m_Passes[passIndex].Accesses)
				{
					if (access.Kind != RGAccessKind::ReadTexture)
					{
						kept[passIndex] = true;
						toVisit.push_back(passIndex);
						break;
					}
				}
			}

			// Reachability: DependsOnPasses already points from a pass to what
			// it needs, so a forward walk from the roots through that edge set
			// gives exactly the closure of passes required to produce every
			// root's output. Anything not reached is unreachable from anything
			// the graph is actually required to produce this frame, and gets
			// culled.
			while (!toVisit.empty())
			{
				const uint32_t passIndex = toVisit.back();
				toVisit.pop_back();
				for (uint32_t dependency : m_Passes[passIndex].DependsOnPasses)
				{
					if (!kept[dependency])
					{
						kept[dependency] = true;
						toVisit.push_back(dependency);
					}
				}
			}
		}

		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
			m_Passes[passIndex].Culled = !kept[passIndex];

		// Kahn's algorithm over the surviving (non-culled) passes, ties broken
		// by registration order so passes with no cross-dependency (e.g. the
		// shadow cascades, each writing a different target) keep the order the
		// SceneRenderer registered them in. The kept set is dependency-closed
		// by construction, so a kept pass's dependencies are never culled --
		// no extra filtering needed on the edges themselves.
		std::vector<uint32_t> inDegree(passCount, 0);
		std::vector<std::vector<uint32_t>> dependents(passCount);
		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
		{
			if (m_Passes[passIndex].Culled)
				continue;

			std::vector<uint32_t>& deps = m_Passes[passIndex].DependsOnPasses;
			std::sort(deps.begin(), deps.end());
			deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
			inDegree[passIndex] = static_cast<uint32_t>(deps.size());
			for (uint32_t dependency : deps)
				dependents[dependency].push_back(passIndex);
		}

		std::vector<uint32_t> ready;
		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
			if (!m_Passes[passIndex].Culled && inDegree[passIndex] == 0)
				ready.push_back(passIndex);

		m_ExecutionOrder.clear();
		m_ExecutionOrder.reserve(passCount);
		while (!ready.empty())
		{
			std::sort(ready.begin(), ready.end());
			const uint32_t passIndex = ready.front();
			ready.erase(ready.begin());
			m_ExecutionOrder.push_back(passIndex);

			for (uint32_t dependent : dependents[passIndex])
			{
				if (--inDegree[dependent] == 0)
					ready.push_back(dependent);
			}
		}

		const uint32_t keptCount = static_cast<uint32_t>(std::count(kept.begin(), kept.end(), true));
		CORE_ASSERT(m_ExecutionOrder.size() == keptCount,
			"RenderGraph::Compile found a resource-dependency cycle");

		// Mark consecutive graphics passes that share their full attachment
		// set (same color + depth resources) as mergeable, so Execute() folds
		// the whole run into one Dynamic Rendering scope instead of one per
		// pass. A pass with no attachments (or a non-graphics pass) never
		// merges. Equality of attachment signatures is transitive, so a run
		// longer than two passes merges correctly from these pairwise checks
		// alone. Skipped entirely when the debug panel's "Enable scope
		// merging" toggle is off -- every Pass::MergesWithPrevious stays at
		// its default false, so Execute() opens one scope per pass.
		if (m_ScopeMergingEnabled)
		{
			auto attachmentSignature = [this](uint32_t passIndex)
			{
				std::vector<uint32_t> signature;
				for (const RGResourceAccess& access : m_Passes[passIndex].Accesses)
				{
					if (access.Kind == RGAccessKind::WriteColorAttachment || access.Kind == RGAccessKind::WriteDepthAttachment)
						signature.push_back(access.Resource.Index);
				}
				std::sort(signature.begin(), signature.end());
				return signature;
			};

			for (uint32_t orderIndex = 1; orderIndex < m_ExecutionOrder.size(); ++orderIndex)
			{
				const uint32_t passIndex = m_ExecutionOrder[orderIndex];
				const uint32_t previousPassIndex = m_ExecutionOrder[orderIndex - 1];
				if (m_Passes[passIndex].Type != Pass::Type::Graphics || m_Passes[previousPassIndex].Type != Pass::Type::Graphics)
					continue;

				const std::vector<uint32_t> signature = attachmentSignature(passIndex);
				if (!signature.empty() && signature == attachmentSignature(previousPassIndex))
					m_Passes[passIndex].MergesWithPrevious = true;
			}
		}

		// Walk resources in compiled order to resolve each access's exact
		// Before/After synchronization scope from real history, instead of a
		// per-call-site guess.
		struct ResourceCompileState
		{
			RGAccessState Access;
			bool IsShaderReadable = false;
			bool WasAccessed = false;
		};
		std::vector<ResourceCompileState> state(m_Resources.size());
		for (uint32_t resourceIndex = 0; resourceIndex < m_Resources.size(); ++resourceIndex)
			state[resourceIndex].Access = m_Resources[resourceIndex].InitialState;

		for (uint32_t passIndex : m_ExecutionOrder)
		{
			for (RGResourceAccess& access : m_Passes[passIndex].Accesses)
			{
				const uint32_t resourceIndex = access.Resource.Index;
				const ResolvedAccess resolved = ResolveAccess(access, m_Resources[resourceIndex].IsDepthFormat);

				access.Before = state[resourceIndex].Access;
				access.After = resolved.Produced;

				state[resourceIndex].Access = resolved.Produced;
				state[resourceIndex].IsShaderReadable = (access.Kind == RGAccessKind::ReadTexture);
				state[resourceIndex].WasAccessed = true;
			}
		}

		// A shader-resource render target nothing in the graph reads back (the
		// frame's final composite output, sampled later by the editor outside
		// this graph) still needs one closing transition to a shader-read
		// layout, mirroring what EndRendering used to do unconditionally for
		// every shader-resource attachment when its scope closed.
		for (uint32_t resourceIndex = 0; resourceIndex < m_Resources.size(); ++resourceIndex)
		{
			Resource& resource = m_Resources[resourceIndex];
			if (resource.IsImage || !resource.IsShaderResource || !state[resourceIndex].WasAccessed)
				continue;
			if (state[resourceIndex].IsShaderReadable)
				continue;

			resource.NeedsExportTransition = true;
			resource.FinalState = state[resourceIndex].Access;
		}

		m_Compiled = true;
	}

	void RenderGraph::Execute(CommandBuffer& commandBuffer, uint32_t frameIndex, RendererFrameStatistics* statistics)
	{
		CORE_ASSERT(m_Compiled, "RenderGraph::Execute called before Compile()");

		// When off, every Pass::GPUTimeQueryID stays at its default
		// InvalidQueryID -- skipped here, not just hidden by the debug panel,
		// since GPU timestamp queries have real cost even when unused (see
		// Core/Profiler.h).
		const bool profilingEnabled = Profiler::IsEnabled();

		GraphicsCommandList graphics(commandBuffer.GetRecordingView(frameIndex), statistics);
		RayTracingCommandList rayTracing(commandBuffer.GetRecordingView(frameIndex), statistics);
		ComputeCommandList compute(commandBuffer.GetRecordingView(frameIndex), statistics);

		// A group is one pass, or a run of consecutive graphics passes sharing
		// one Dynamic Rendering scope (Pass::MergesWithPrevious, set by
		// Compile()). A barrier cannot legally target an image that is
		// currently bound as an active Dynamic Rendering attachment, so every
		// group member's barriers are recorded up front, before the group's
		// (possibly shared) scope opens.
		for (uint32_t orderIndex = 0; orderIndex < m_ExecutionOrder.size(); )
		{
			uint32_t groupEnd = orderIndex + 1;
			while (groupEnd < m_ExecutionOrder.size() && m_Passes[m_ExecutionOrder[groupEnd]].MergesWithPrevious)
				++groupEnd;

			for (uint32_t member = orderIndex; member < groupEnd; ++member)
			{
				for (const RGResourceAccess& access : m_Passes[m_ExecutionOrder[member]].Accesses)
					InsertBarrier(m_Resources[access.Resource.Index], access, graphics);
			}

			if (m_Passes[m_ExecutionOrder[orderIndex]].Type == Pass::Type::Graphics)
			{
				// Build one RenderingInfo for the whole group: a resource
				// shared by several members (Sky and Forward both target
				// SceneColor/SceneDepth) appears once, taking its LoadOp from
				// the first member that declared it and its StoreOp from the
				// last -- exactly what "several logical passes, one scope"
				// means for a Dynamic Rendering attachment.
				RenderingInfo renderingInfo;
				for (uint32_t member = orderIndex; member < groupEnd; ++member)
				{
					for (const RGResourceAccess& access : m_Passes[m_ExecutionOrder[member]].Accesses)
					{
						const Resource& resource = m_Resources[access.Resource.Index];
						if (access.Kind == RGAccessKind::WriteColorAttachment)
						{
							auto existing = std::find_if(renderingInfo.ColorAttachments.begin(), renderingInfo.ColorAttachments.end(),
								[&](const RenderingAttachmentInfo& info) { return info.Target == resource.RenderTarget; });
							if (existing == renderingInfo.ColorAttachments.end())
								renderingInfo.ColorAttachments.push_back({ resource.RenderTarget, access.LoadOp, access.StoreOp });
							else
								existing->StoreOp = access.StoreOp;
						}
						else if (access.Kind == RGAccessKind::WriteDepthAttachment)
						{
							if (!renderingInfo.DepthAttachment.has_value())
								renderingInfo.DepthAttachment = RenderingAttachmentInfo{ resource.RenderTarget, access.LoadOp, access.StoreOp };
							else
								renderingInfo.DepthAttachment->StoreOp = access.StoreOp;
						}
					}
				}

				const bool hasAttachments = !renderingInfo.ColorAttachments.empty() || renderingInfo.DepthAttachment.has_value();
				if (hasAttachments)
				{
					const Handle<RenderTargetTexture> sizeSource = !renderingInfo.ColorAttachments.empty()
						? renderingInfo.ColorAttachments.front().Target
						: renderingInfo.DepthAttachment->Target;
					RenderTargetTexture* sizeTarget = ResourceManager::GetRenderTarget(sizeSource);
					CORE_ASSERT(sizeTarget, "RenderGraph graphics pass references an invalid render target");
					renderingInfo.Width = sizeTarget->GetWidth();
					renderingInfo.Height = sizeTarget->GetHeight();

					graphics.BeginRenderingAttachments(renderingInfo);
					for (uint32_t member = orderIndex; member < groupEnd; ++member)
					{
						Pass& pass = m_Passes[m_ExecutionOrder[member]];
						if (profilingEnabled)
							pass.GPUTimeQueryID = commandBuffer.BeginTimestampQueryRenderThread();
						pass.GraphicsExecute(graphics);
						if (profilingEnabled)
							commandBuffer.EndTimestampQueryRenderThread(pass.GPUTimeQueryID);
					}
					graphics.EndRenderingAttachments();
				}
				else
				{
					for (uint32_t member = orderIndex; member < groupEnd; ++member)
					{
						Pass& pass = m_Passes[m_ExecutionOrder[member]];
						if (profilingEnabled)
							pass.GPUTimeQueryID = commandBuffer.BeginTimestampQueryRenderThread();
						pass.GraphicsExecute(graphics);
						if (profilingEnabled)
							commandBuffer.EndTimestampQueryRenderThread(pass.GPUTimeQueryID);
					}
				}
			}
			else
			{
				// Compute/RayTracing passes never merge (see Compile()), so
				// this group always has exactly one member.
				Pass& pass = m_Passes[m_ExecutionOrder[orderIndex]];
				if (profilingEnabled)
					pass.GPUTimeQueryID = commandBuffer.BeginTimestampQueryRenderThread();
				if (pass.Type == Pass::Type::Compute)
					pass.ComputeExecute(compute);
				else
					pass.RayTracingExecute(rayTracing);
				if (profilingEnabled)
					commandBuffer.EndTimestampQueryRenderThread(pass.GPUTimeQueryID);
			}

			orderIndex = groupEnd;
		}

		for (const Resource& resource : m_Resources)
		{
			if (!resource.NeedsExportTransition)
				continue;

			Image* image = ResolveBackingImage(resource);
			CORE_ASSERT(image, "RenderGraph export resource has no backing image");

			const VkImageLayout newLayout = resource.IsDepthFormat
				? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			image->InsertImageMemoryBarrier(
				graphics,
				resource.FinalState.Access,
				VK_ACCESS_SHADER_READ_BIT,
				newLayout,
				resource.FinalState.Stage,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				SubresourceRangeFor(resource.IsDepthFormat));
		}
	}

	void RenderGraph::Reset()
	{
		m_Resources.clear();
		m_Passes.clear();
		m_ExecutionOrder.clear();
		m_Compiled = false;
	}

	void RenderGraph::OnImGuiRender(CommandBuffer& commandBuffer)
	{
		if (!ui::PropertyGridHeader("RenderGraph", true))
			return;

		// Takes effect on the next Compile() (this frame's pass list below is
		// still the one compiled with whatever these were set to last frame).
		// There is no equivalent toggle for pass ordering/barrier computation
		// itself -- increment 1 replaced the old hand-sequenced path rather
		// than keeping it as a fallback, so these two are the parts of the
		// graph that are genuinely optional to compare with/without live.
		ImGui::Checkbox("Enable scope merging", &m_ScopeMergingEnabled);
		ImGui::Checkbox("Enable pass culling", &m_PassCullingEnabled);
		ImGui::Spacing();

		const uint32_t culledCount = static_cast<uint32_t>(
			std::count_if(m_Passes.begin(), m_Passes.end(), [](const Pass& pass) { return pass.Culled; }));
		ImGui::Text("%zu passes compiled, %u culled", m_ExecutionOrder.size(), culledCount);

		if (ImGui::BeginTable("RenderGraphPasses", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Pass");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Scope");
			ImGui::TableSetupColumn("GPU Time");
			ImGui::TableHeadersRow();

			for (uint32_t passIndex : m_ExecutionOrder)
			{
				const Pass& pass = m_Passes[passIndex];
				const char* typeName = pass.Type == Pass::Type::Graphics ? "Graphics"
					: pass.Type == Pass::Type::Compute ? "Compute" : "RayTracing";

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(pass.Name.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(typeName);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(pass.MergesWithPrevious ? "merged" : "-");
				ImGui::TableNextColumn();
				if (pass.GPUTimeQueryID != InvalidQueryID)
					ImGui::Text("%.3f ms", commandBuffer.GetExecutionGPUTime(pass.GPUTimeQueryID));
				else
					ImGui::TextUnformatted("-");
			}
			ImGui::EndTable();
		}

		// Per-pass declared accesses and the barrier Compile() resolved for
		// each -- useful for inspecting exactly what synchronization a pass
		// (e.g. one of the shadow cascades) actually got.
		ImGui::Spacing();
		ImGui::TextUnformatted("Pass details:");
		for (uint32_t passIndex : m_ExecutionOrder)
		{
			const Pass& pass = m_Passes[passIndex];
			if (!ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(passIndex)), "%s", pass.Name.c_str()))
				continue;

			for (const RGResourceAccess& access : pass.Accesses)
			{
				const Resource& resource = m_Resources[access.Resource.Index];
				ImGui::BulletText("%s [%s]", resource.Name.c_str(), AccessKindName(access.Kind));
				ImGui::Indent();
				ImGui::TextDisabled("before: %s @ %s", DescribeAccessFlags(access.Before.Access).c_str(), DescribeStageFlags(access.Before.Stage).c_str());
				ImGui::TextDisabled(" after: %s @ %s", DescribeAccessFlags(access.After.Access).c_str(), DescribeStageFlags(access.After.Stage).c_str());
				ImGui::Unindent();
			}
			ImGui::TreePop();
		}

		if (culledCount > 0)
		{
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f),
				"Culled (unreachable from any pass that writes an imported resource):");
			for (const Pass& pass : m_Passes)
			{
				if (pass.Culled)
					ImGui::BulletText("%s", pass.Name.c_str());
			}
		}

		ui::EndTreeNode();
	}
}

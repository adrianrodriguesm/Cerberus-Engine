#include "pch.h"
#include "RenderGraph.h"

#include "Renderer/RHI/Command/CommandBuffer.h"
#include "Renderer/RHI/Command/ComputeCommandList.h"
#include "Renderer/RHI/Command/GraphicsCommandList.h"
#include "Renderer/RHI/Command/RayTracingCommandList.h"
#include "Renderer/RHI/Image.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/RHI/Texture.h"

#include <algorithm>

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

		// Kahn's algorithm, ties broken by registration order so passes with no
		// cross-dependency (e.g. the shadow cascades, each writing a different
		// target) keep the order the SceneRenderer registered them in.
		std::vector<uint32_t> inDegree(passCount, 0);
		std::vector<std::vector<uint32_t>> dependents(passCount);
		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
		{
			std::vector<uint32_t>& deps = m_Passes[passIndex].DependsOnPasses;
			std::sort(deps.begin(), deps.end());
			deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
			inDegree[passIndex] = static_cast<uint32_t>(deps.size());
			for (uint32_t dependency : deps)
				dependents[dependency].push_back(passIndex);
		}

		std::vector<uint32_t> ready;
		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
			if (inDegree[passIndex] == 0)
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

		CORE_ASSERT(m_ExecutionOrder.size() == passCount,
			"RenderGraph::Compile found a resource-dependency cycle");

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

		GraphicsCommandList graphics(commandBuffer.GetRecordingView(frameIndex), statistics);
		RayTracingCommandList rayTracing(commandBuffer.GetRecordingView(frameIndex), statistics);
		ComputeCommandList compute(commandBuffer.GetRecordingView(frameIndex), statistics);

		for (uint32_t passIndex : m_ExecutionOrder)
		{
			Pass& pass = m_Passes[passIndex];

			// Barriers are plain vkCmdPipelineBarrier calls against the shared
			// command buffer; which typed wrapper issues them does not matter.
			for (const RGResourceAccess& access : pass.Accesses)
				InsertBarrier(m_Resources[access.Resource.Index], access, graphics);

			switch (pass.Type)
			{
			case Pass::Type::Graphics:
			{
				RenderingInfo renderingInfo;
				for (const RGResourceAccess& access : pass.Accesses)
				{
					const Resource& resource = m_Resources[access.Resource.Index];
					if (access.Kind == RGAccessKind::WriteColorAttachment)
					{
						renderingInfo.ColorAttachments.push_back(
							{ resource.RenderTarget, access.LoadOp, access.StoreOp });
					}
					else if (access.Kind == RGAccessKind::WriteDepthAttachment)
					{
						renderingInfo.DepthAttachment =
							RenderingAttachmentInfo{ resource.RenderTarget, access.LoadOp, access.StoreOp };
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
					pass.GraphicsExecute(graphics);
					graphics.EndRenderingAttachments();
				}
				else
				{
					pass.GraphicsExecute(graphics);
				}
				break;
			}
			case Pass::Type::Compute:
				pass.ComputeExecute(compute);
				break;
			case Pass::Type::RayTracing:
				pass.RayTracingExecute(rayTracing);
				break;
			}
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
}

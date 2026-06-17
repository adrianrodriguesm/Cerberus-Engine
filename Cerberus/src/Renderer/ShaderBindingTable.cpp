#include "pch.h"
#include "ShaderBindingTable.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/VulkanExtensions.h"
#include "RenderQueueManager.h"

namespace cerberus
{
	namespace utils
	{
		static size_t RoundUp(size_t size, size_t powerOf2Alignment)
		{
			return (size + powerOf2Alignment - 1) & ~(powerOf2Alignment - 1);
		}

		static size_t GetEntrySize(
			const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
			const std::vector<SBTEntry>& entries)
		{
			if (entries.empty())
				return 0;

			// Find the maximum number of inline parameters used by a single entry.
			size_t maxArgs = 0;
			for (const auto& entry : entries)
			{
				maxArgs = std::max(maxArgs, entry.InlineData.size());
			}

			// A shader record is the shader group handle plus optional inline data.
			// Each record stride must be aligned to shaderGroupHandleAlignment.
			return RoundUp(
				rayTracingProperties.shaderGroupHandleSize + maxArgs,
				rayTracingProperties.shaderGroupHandleAlignment);
		}

		static size_t CopyShaderData(
			uint8_t* const dst,
			const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
			const std::vector<SBTEntry>& entries,
			const size_t entrySize,
			const uint8_t* const shaderHandleStorage)
		{
			if (entries.empty())
				return 0;

			const auto handleSize = rayTracingProperties.shaderGroupHandleSize;

			uint8_t* pDst = dst;

			for (const auto& entry : entries)
			{
				// Copy the shader identifier that was previously obtained with
				// vkGetRayTracingShaderGroupHandlesKHR.
				std::memcpy(pDst, shaderHandleStorage + entry.GroupIndex * handleSize, handleSize);

				if (!entry.InlineData.empty())
				{
					std::memcpy(pDst + handleSize, entry.InlineData.data(), entry.InlineData.size());
				}

				pDst += entrySize;
			}

			return entries.size() * entrySize;
		}

		static uint32_t GetRequiredShaderGroupHandleCount(const ShaderBindingTableSpecification& spec)
		{
			uint32_t maxGroupIndex = 0;
			bool foundAnyGroup = false;

			auto visitEntries = [&](const std::vector<SBTEntry>& entries)
				{
					for (const auto& entry : entries)
					{
						maxGroupIndex = std::max(maxGroupIndex, entry.GroupIndex);
						foundAnyGroup = true;
					}
				};

			visitEntries(spec.RayGenPrograms);
			visitEntries(spec.MissPrograms);
			visitEntries(spec.HitPrograms);

			CORE_ASSERT(foundAnyGroup, "ShaderBindingTable requires at least one shader group");
			return maxGroupIndex + 1;
		}
	}

	ShaderBindingTable::ShaderBindingTable(const ShaderBindingTableSpecification& spec)
		: m_Spec(spec)
	{
		Invalidate();
	}

	ShaderBindingTable::~ShaderBindingTable()
	{
		Release();
	}

	void ShaderBindingTable::Invalidate()
	{
		Release();

		const auto& rtProperties = RendererContext::GetCurrentDevice()->GetRayTracingProperties();

		// Record stride: aligned to shaderGroupHandleAlignment.
		const size_t rayGenEntrySize = utils::GetEntrySize(rtProperties, m_Spec.RayGenPrograms);
		const size_t missEntrySize = utils::GetEntrySize(rtProperties, m_Spec.MissPrograms);
		const size_t hitGroupEntrySize = utils::GetEntrySize(rtProperties, m_Spec.HitPrograms);

		const size_t baseAlignment = rtProperties.shaderGroupBaseAlignment;

		// Region base addresses used by vkCmdTraceRaysKHR must be aligned to
		// shaderGroupBaseAlignment. Therefore each SBT region must also be padded to
		// shaderGroupBaseAlignment, even when it only contains one shader record.
		const size_t rayGenOffset = 0;
		const size_t rayGenRegionSize = utils::RoundUp(rayGenEntrySize * m_Spec.RayGenPrograms.size(), baseAlignment);

		const size_t missOffset = rayGenOffset + rayGenRegionSize;
		const size_t missRegionSize = utils::RoundUp(missEntrySize * m_Spec.MissPrograms.size(), baseAlignment);

		const size_t hitGroupOffset = missOffset + missRegionSize;
		const size_t hitGroupRegionSize = utils::RoundUp(hitGroupEntrySize * m_Spec.HitPrograms.size(), baseAlignment);

		const size_t sbtSize = hitGroupOffset + hitGroupRegionSize;

		CORE_ASSERT(sbtSize > 0, "ShaderBindingTable size is zero");
		CORE_ASSERT((rayGenOffset % baseAlignment) == 0, "SBT raygen region is not base-aligned");
		CORE_ASSERT((missOffset % baseAlignment) == 0, "SBT miss region is not base-aligned");
		CORE_ASSERT((hitGroupOffset % baseAlignment) == 0, "SBT hit region is not base-aligned");

		// We need enough handles to cover the highest shader group index referenced by
		// the SBT entries. This is safer than assuming the entries are contiguous.
		const uint32_t handleSize = rtProperties.shaderGroupHandleSize;
		const uint32_t shaderGroupHandleCount = utils::GetRequiredShaderGroupHandleCount(m_Spec);
		std::vector<uint8_t> shaderHandleStorage(shaderGroupHandleCount * handleSize);

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		RayTracingPipeline* rtPipeline = ResourceManager::GetRayTracingPipeline(m_Spec.Pipeline);
		CORE_ASSERT(rtPipeline, "Invalid pipeline");

		VK_CHECK_RESULT(ext::vkGetRayTracingShaderGroupHandlesKHR(
			vulkanDevice,
			rtPipeline->GetHandle(),
			0,
			shaderGroupHandleCount,
			shaderHandleStorage.size(),
			shaderHandleStorage.data()));

		GPUBufferSpecification gpuBufferSpec;
		gpuBufferSpec.Usage = GPUBufferUsage::ShaderBindingTableUsage;
		gpuBufferSpec.DebugName = "Shader Binding Table";
		gpuBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		gpuBufferSpec.Size = sbtSize;

		m_Buffer = ResourceManager::CreateBuffer(gpuBufferSpec);
		GPUBuffer* sbtBuffer = ResourceManager::GetBuffer(m_Buffer);
		CORE_ASSERT(sbtBuffer, "Failed to create SBT buffer");

		ResourceAllocator allocator("Shader Binding Table Buffer");

		uint8_t* mappedData = allocator.MapMemory<uint8_t>(sbtBuffer->Allocation);
		CORE_ASSERT(mappedData, "Failed to map SBT buffer");

		// Important: zero the padded regions. The command buffer reads miss/hit from
		// base-aligned offsets, so we must copy shader handles to those exact offsets.
		std::memset(mappedData, 0, sbtSize);

		utils::CopyShaderData(
			mappedData + rayGenOffset,
			rtProperties,
			m_Spec.RayGenPrograms,
			rayGenEntrySize,
			shaderHandleStorage.data());

		utils::CopyShaderData(
			mappedData + missOffset,
			rtProperties,
			m_Spec.MissPrograms,
			missEntrySize,
			shaderHandleStorage.data());

		utils::CopyShaderData(
			mappedData + hitGroupOffset,
			rtProperties,
			m_Spec.HitPrograms,
			hitGroupEntrySize,
			shaderHandleStorage.data());

		allocator.UnmapMemory(sbtBuffer->Allocation);
	}

	void ShaderBindingTable::Release()
	{
		if (!m_Buffer.IsValid())
			return;

		Handle<GPUBuffer> bufferToDestroy = m_Buffer;
		m_Buffer = Handle<GPUBuffer>{};

		ResourceManager::DestroyBuffer(bufferToDestroy);
	}
}

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

		static size_t GetEntrySize(const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties, const std::vector<SBTEntry>& entries)
		{
			// Find the maximum number of parameters used by a single entry
			size_t maxArgs = 0;

			for (const auto& entry : entries)
			{
				maxArgs = std::max(maxArgs, entry.InlineData.size());
			}

			// A SBT entry is made of a program ID and a set of 4-byte parameters (see shaderRecordEXT).
			// Its size is ShaderGroupHandleSize (plus parameters) and must be aligned to ShaderGroupBaseAlignment.
			return RoundUp(rayTracingProperties.shaderGroupHandleSize + maxArgs, rayTracingProperties.shaderGroupHandleSize);
		}

		static size_t CopyShaderData(
			uint8_t* const dst,
			const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
			const std::vector<SBTEntry>& entries,
			const size_t entrySize,
			const uint8_t* const shaderHandleStorage)
		{
			const auto handleSize = rayTracingProperties.shaderGroupHandleSize;

			uint8_t* pDst = dst;

			for (const auto& entry : entries)
			{
				// Copy the shader identifier that was previously obtained with vkGetRayTracingShaderGroupHandlesKHR.
				std::memcpy(pDst, shaderHandleStorage + entry.GroupIndex * handleSize, handleSize);
				std::memcpy(pDst + handleSize, entry.InlineData.data(), entry.InlineData.size());

				pDst += entrySize;
			}

			return entries.size() * entrySize;
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

		// A SBT entry is made of a program ID and a set of 4-byte parameters (see shaderRecordEXT).
		// Its size is ShaderGroupHandleSize (plus parameters) and must be aligned to ShaderGroupBaseAlignment.
		size_t rayGenEntrySize = utils::GetEntrySize(rtProperties, m_Spec.RayGenPrograms);
		size_t missEntrySize = utils::GetEntrySize(rtProperties, m_Spec.MissPrograms);
		size_t hitGroupEntrySize = utils::GetEntrySize(rtProperties, m_Spec.HitPrograms);


		size_t rayGenOffset = 0;
		size_t missOffset = m_Spec.RayGenPrograms.size() * rayGenEntrySize;
		size_t hitGroupOffset = missOffset + (m_Spec.MissPrograms.size() * missEntrySize);

		// Compute the size of the table.
		const size_t sbtSize = rayGenEntrySize * m_Spec.RayGenPrograms.size() + missEntrySize * m_Spec.MissPrograms.size() + hitGroupEntrySize * m_Spec.HitPrograms.size();


		// Generate the table.
		const uint32_t handleSize = rtProperties.shaderGroupHandleSize;
		const size_t groupCount = m_Spec.RayGenPrograms.size() + m_Spec.MissPrograms.size() + m_Spec.HitPrograms.size();
		std::vector<uint8_t> shaderHandleStorage(groupCount * handleSize);

		VkDevice vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		RayTracingPipeline* rtPipeline = ResourceManager::GetRayTracingPipeline(m_Spec.Pipeline);
		CORE_ASSERT(rtPipeline, "Invalid pipeline")

		VK_CHECK_RESULT(ext::vkGetRayTracingShaderGroupHandlesKHR(
			vulkanDevice,
			rtPipeline->GetHandle(),
			0, static_cast<uint32_t>(groupCount),
			shaderHandleStorage.size(),
			shaderHandleStorage.data()));

		GPUBufferSpecification gpuBufferSpec;
		gpuBufferSpec.Usage = GPUBufferUsage::ShaderBindingTableUsage;
		gpuBufferSpec.DebugName = "Shader Binding Table";
		gpuBufferSpec.MemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		gpuBufferSpec.Size = sbtSize;

		m_Buffer = ResourceManager::CreateBuffer(gpuBufferSpec);
		GPUBuffer* sbtBuffer = ResourceManager::GetBuffer(m_Buffer);
		
		ResourceAllocator allocator("Shader Binding Table Buffer");

		// Copy data to stb buffer
		// 1. Create pointer to a point in normal memory and  "Map" the staging buffer memory to that point
		uint8_t* destData = allocator.MapMemory<uint8_t>(sbtBuffer->Allocation);
		// 2. Copy memory to the point
		destData += utils::CopyShaderData(destData, rtProperties, m_Spec.RayGenPrograms, rayGenEntrySize, shaderHandleStorage.data());
		destData += utils::CopyShaderData(destData, rtProperties, m_Spec.MissPrograms, missEntrySize, shaderHandleStorage.data());
		utils::CopyShaderData(destData, rtProperties, m_Spec.HitPrograms, hitGroupEntrySize, shaderHandleStorage.data());
		// 3. Unmap the buffer memory
		allocator.UnmapMemory(sbtBuffer->Allocation);
	}

	void ShaderBindingTable::Release()
	{
		if (GPUBuffer* stbBuffer = ResourceManager::GetBuffer(m_Buffer))
		{
			RenderQueueManager::SubmitResourceFree([this]() mutable
				{
					ResourceManager::DestroyBuffer(m_Buffer);
				});
		}
	}

}


#pragma once
#include "Renderer/RHI/Buffers/GPUBuffer.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"
namespace cerberus
{
	struct SBTEntry
	{
		uint32_t GroupIndex;
		std::vector<unsigned char> InlineData;
	};

	struct ShaderBindingTableSpecification
	{
		Handle<RayTracingPipeline> Pipeline;
		std::vector<SBTEntry> RayGenPrograms, MissPrograms, HitPrograms;
	};

	struct ShaderBindingTableRegions
	{
		VkStridedDeviceAddressRegionKHR RayGen{};
		VkStridedDeviceAddressRegionKHR Miss{};
		VkStridedDeviceAddressRegionKHR Hit{};
		VkStridedDeviceAddressRegionKHR Callable{};
	};

	class ShaderBindingTable final
	{
	public:
		ShaderBindingTable(const ShaderBindingTableSpecification& spec);
		~ShaderBindingTable();

		Handle<GPUBuffer> GetGPUBuffer() const { return m_Buffer; }
		const ShaderBindingTableRegions& GetRegions() const { return m_Regions; }
	private:
		void Invalidate();
		void Release();

	private:
		ShaderBindingTableSpecification m_Spec;
		Handle<GPUBuffer> m_Buffer;
		ShaderBindingTableRegions m_Regions{};
	};

}

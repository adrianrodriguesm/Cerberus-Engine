#pragma once
#include "Renderer\GPUBuffer.h"
#include "Renderer\Pipeline.h"
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

	class ShaderBindingTable final
	{
	public:
		ShaderBindingTable(const ShaderBindingTableSpecification& spec);
		~ShaderBindingTable();

		Handle<GPUBuffer> GetGPUBuffer() const { return m_Buffer; }
	private:
		void Invalidate();
		void Release();

	private:
		ShaderBindingTableSpecification m_Spec;
		Handle<GPUBuffer> m_Buffer;
	};

}
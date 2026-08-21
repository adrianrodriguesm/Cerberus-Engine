#pragma once

#include "Core/Base.h"
#include "Renderer/Shader.h"
namespace cerberus
{
	// Passive compute pipeline resource. Command recording and submission belong
	// to ComputeCommandList and the owning command-buffer infrastructure.
	class ComputePipeline
	{
	public:
		ComputePipeline(Ref<Shader> computeShader);
		~ComputePipeline();

		ComputePipeline(const ComputePipeline&) = delete;
		ComputePipeline& operator=(const ComputePipeline&) = delete;
		ComputePipeline(ComputePipeline&&) = delete;
		ComputePipeline& operator=(ComputePipeline&&) = delete;

		void Invalidate();

		Ref<Shader> GetShader() const { return m_Shader; }
		VkPipeline GetHandle() const { return m_ComputePipeline; }
		VkPipelineLayout GetVulkanPipelineLayout() const { return m_ComputePipelineLayout; }
	private:
		Ref<Shader> m_Shader;
		VkPipelineLayout m_ComputePipelineLayout = VK_NULL_HANDLE;
		VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
		VkPipeline m_ComputePipeline = VK_NULL_HANDLE;
	};

}

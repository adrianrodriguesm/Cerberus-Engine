#pragma once
#include "Renderer/VertexBuffer.h"
#include "Renderer/Shader.h"
#include "Renderer/RenderPass.h"
namespace cerberus
{
	enum class PrimitiveTopology
	{
		None = 0,
		Points,
		Lines,
		Triangles,
		LineStrip,
		TriangleStrip,
		TriangleFan
	};

	enum class CullMode
	{
		None = 0,
		Front = 1,
		Back = 2
	};

	enum class DepthCompareOperator
	{
		None = 0,
		Never,
		NotEqual,
		Less,
		LessOrEqual,
		Greater,
		GreaterOrEqual,
		Equal,
		Always,
	};

	struct PipelineSpecification
	{
		Ref<Shader> Shader;
		VertexBufferLayout Layout;

		Handle<RenderPass> RenderPass;

		PrimitiveTopology Topology = PrimitiveTopology::Triangles;
		DepthCompareOperator DepthOperator = DepthCompareOperator::LessOrEqual;
		CullMode CullMode = CullMode::None;
		bool DepthTest = true;
		bool DepthWrite = true;
		bool Wireframe = false;
		float LineWidth = 1.0f;
		std::string DebugName;
	};

	class GraphicsPipeline
	{
	public:
		GraphicsPipeline(PipelineSpecification spec);
		virtual ~GraphicsPipeline();

		PipelineSpecification& GetSpecification() { return m_Specification; }
		const PipelineSpecification& GetSpecification() const { return m_Specification; }

		void Invalidate();

		VkPipeline GetHandle() { return m_Pipeline; }
		VkPipelineLayout GetVulkanPipelineLayout() { return m_PipelineLayout; }
	private:
		PipelineSpecification m_Specification;

		VkPipelineLayout m_PipelineLayout = nullptr;
		VkPipelineCache m_PipelineCache = nullptr;
		VkPipeline m_Pipeline = nullptr;
	};

	class RayTracingPipeline
	{
	public:
		RayTracingPipeline(const PipelineSpecification& spec);
		virtual ~RayTracingPipeline();

		PipelineSpecification& GetSpecification() { return m_Specification; }
		const PipelineSpecification& GetSpecification() const { return m_Specification; }

		void Invalidate();

		VkPipeline GetHandle() { return m_rtPipeline; }
		VkPipelineLayout GetVulkanPipelineLayout() { return m_rtPipelineLayout; }

		uint32_t RayGenShaderIndex() const { return m_RayGenIndex; }
		uint32_t MissShaderIndex() const { return m_MissHitIndex; }
		uint32_t TriangleHitGroupIndex() const { return m_TriangleHitIndex; }
	private:
		PipelineSpecification m_Specification;

		VkPipelineLayout m_rtPipelineLayout = nullptr;
		VkPipelineCache m_rtPipelineCache = nullptr;
		VkPipeline m_rtPipeline = nullptr;

		uint32_t m_RayGenIndex, m_MissHitIndex, m_TriangleHitIndex;
	};









}
#pragma once
#include "Renderer/Backend/BindGroup.h"
namespace cerberus
{
	enum class ShaderStage : uint8_t
	{
		VERTEX,
		FRAGMENT,
		COMPUTE,
	};

	struct ShaderData
	{
		uint8_t ByteCode;
		ShaderStage Stage;
	};


	class ShaderReflection
	{
	public:
		// Cache the data from push constant and the 
		static BindGroupLayout::BindGroupLayoutInfo ReflectLayoutInfo(Ref<Shader> shader, uint32_t set);
		static void ReflectLayoutInfoPerShaderStage(VkShaderStageFlagBits shaderStage, const std::vector<uint32_t>& shaderData, BindGroupLayout::BindGroupLayoutInfo& layoutInfo);
	};
}
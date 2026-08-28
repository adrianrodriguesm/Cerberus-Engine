#include "pch.h"
#include "ShaderCompiler.h"
#include "Renderer/Core/RendererContext.h"
#include <spirv_cross/spirv_glsl.hpp>
#include <shaderc/shaderc.hpp>

namespace cerberus
{
	BindGroupLayout::BindGroupLayoutInfo ShaderReflection::ReflectLayoutInfo(Ref<Shader> shader, uint32_t set)
	{
		// Get Shader Data from cache
		std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>> shaderData = shader->GetShaderByteCode();
		
		BindGroupLayout::BindGroupLayoutInfo layoutInfo;
		layoutInfo.Set = set;

		for (const auto& [stage, data] : shaderData)
			ReflectLayoutInfoPerShaderStage(stage, data, layoutInfo);
		
		
		return layoutInfo;
	}
	void ShaderReflection::ReflectLayoutInfoPerShaderStage(VkShaderStageFlagBits shaderStage, const std::vector<uint32_t>& shaderData, BindGroupLayout::BindGroupLayoutInfo& outLayoutInfo)
	{
		NV_CORE_TRACE("===========================");
		NV_CORE_TRACE(" Shader Reflection");
		NV_CORE_TRACE("===========================");

		spirv_cross::Compiler compiler(shaderData);
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		NV_CORE_TRACE("Uniform Buffers:");
		for (const auto& resource : resources.uniform_buffers)
		{
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			
			if (descriptorSet != outLayoutInfo.Set)
				continue;

			BindGroupLayout::BufferBinding bufferBiding;
			bufferBiding.Name = resource.name;
			bufferBiding.BindingPoint = compiler.get_decoration(resource.id, spv::DecorationBinding);
			bufferBiding.ShaderStage = VK_SHADER_STAGE_ALL;
			bufferBiding.UsageType = BufferUsageType::Static;

			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();
			uint32_t size = (uint32_t)compiler.get_declared_struct_size(bufferType);

			NV_CORE_TRACE("  {0} ({1}, {2})", resource.name, descriptorSet, bufferBiding.BindingPoint);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("  Size: {0}", size);
			NV_CORE_TRACE("-------------------");

			outLayoutInfo.UniformBuffers.push_back(bufferBiding);
		}

		NV_CORE_TRACE("Storage Buffers:");
		for (const auto& resource : resources.storage_buffers)
		{
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);

			if (descriptorSet != outLayoutInfo.Set)
				continue;

			BindGroupLayout::BufferBinding bufferBiding;
			bufferBiding.Name = resource.name;
			bufferBiding.BindingPoint = compiler.get_decoration(resource.id, spv::DecorationBinding);
			bufferBiding.ShaderStage = VK_SHADER_STAGE_ALL;
			bufferBiding.UsageType = BufferUsageType::Static;

			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();
			uint32_t size = (uint32_t)compiler.get_declared_struct_size(bufferType);

			NV_CORE_TRACE("  {0} ({1}, {2})", resource.name, descriptorSet, bufferBiding.BindingPoint);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("  Size: {0}", size);
			NV_CORE_TRACE("-------------------");

			outLayoutInfo.StorageBuffers.push_back(bufferBiding);
		}

		NV_CORE_TRACE("Sampled Images:");
		for (const auto& resource : resources.sampled_images)
		{

			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			if (descriptorSet != outLayoutInfo.Set)
				continue;

			auto& type = compiler.get_type(resource.type_id);
				const bool bindless = !type.array.empty() && type.array[0] == 0;
				const uint32_t arraySize = bindless
					? RendererContext::GetCurrentDevice()->GetBindlessTextureCapacity()
					: (type.array.empty() ? 1u : type.array[0]);

			BindGroupLayout::ImageBinding imgBinding;
			imgBinding.Name = resource.name;
			imgBinding.BindingPoint = compiler.get_decoration(resource.id, spv::DecorationBinding);
			imgBinding.ShaderStage = shaderStage;
			imgBinding.ArraySize = arraySize;
			imgBinding.Bindless = bindless;

			NV_CORE_TRACE("  {0} ({1}, {2})", imgBinding.Name, descriptorSet, imgBinding.BindingPoint);

			outLayoutInfo.ImageSamplers.push_back(imgBinding);
		}

		NV_CORE_TRACE("Storage Images:");
		for (const auto& resource : resources.storage_images)
		{
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			if (descriptorSet != outLayoutInfo.Set)
				continue;

			BindGroupLayout::ImageBinding imgBinding;
			imgBinding.Name = resource.name;
			imgBinding.BindingPoint = compiler.get_decoration(resource.id, spv::DecorationBinding);
			imgBinding.ShaderStage = shaderStage;
			imgBinding.ArraySize = 1;

			NV_CORE_TRACE("  {0} ({1}, {2})", imgBinding.Name, descriptorSet, imgBinding.BindingPoint);
			outLayoutInfo.ImageStorages.push_back(imgBinding);
		}


		NV_CORE_TRACE("Acceleration Structures:");
		for (const auto& resource : resources.acceleration_structures)
		{
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			if (descriptorSet != outLayoutInfo.Set)
				continue;

			BindGroupLayout::ASBinding asBinding;
			asBinding.Name = resource.name;
			asBinding.BindingPoint = compiler.get_decoration(resource.id, spv::DecorationBinding);
			asBinding.ShaderStage = shaderStage;

			NV_CORE_TRACE("  {0} ({1}, {2})", asBinding.Name, descriptorSet, asBinding.BindingPoint);
			outLayoutInfo.AccelerationStructures.push_back(asBinding);
		}

		NV_CORE_TRACE("===========================");
	}
}

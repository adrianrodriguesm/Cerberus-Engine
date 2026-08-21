#include "pch.h"
#include "Shader.h"

#include "Renderer/RendererContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/DeferredResourceDeletionQueue.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/DescriptorManager.h"
#include <spirv_cross/spirv_glsl.hpp>
#include <shaderc/shaderc.hpp>
#include <libshaderc_util/file_finder.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "GLSLIncludeProcessor.h"
#include "Utils/FileSystem.h"

namespace cerberus
{
	namespace utils
	{
		
		static std::filesystem::path GetCacheDirectory()
		{
			// Engine-owned shader cache. Project shaders can still be loaded, but compiled
			// binaries live in a single engine cache for now.
			return FileSystem::GetEngineRoot() / "Cache" / "Shader";
		}

		static void CreateCacheDirectoryIfNeeded()
		{
			std::filesystem::path cacheDirectory = GetCacheDirectory();
			if (!std::filesystem::exists(cacheDirectory))
				std::filesystem::create_directories(cacheDirectory);
		}

		static ShaderUniformType SPIRTypeToShaderUniformType(spirv_cross::SPIRType type)
		{
			switch (type.basetype)
			{
			case spirv_cross::SPIRType::Boolean:  return ShaderUniformType::Bool;
			case spirv_cross::SPIRType::Int:
				if (type.vecsize == 1)            return ShaderUniformType::Int;
				if (type.vecsize == 2)            return ShaderUniformType::IVec2;
				if (type.vecsize == 3)            return ShaderUniformType::IVec3;
				if (type.vecsize == 4)            return ShaderUniformType::IVec4;

			case spirv_cross::SPIRType::UInt:     return ShaderUniformType::UInt;
			case spirv_cross::SPIRType::Float:
				if (type.columns == 3)            return ShaderUniformType::Mat3;
				if (type.columns == 4)            return ShaderUniformType::Mat4;

				if (type.vecsize == 1)            return ShaderUniformType::Float;
				if (type.vecsize == 2)            return ShaderUniformType::Vec2;
				if (type.vecsize == 3)            return ShaderUniformType::Vec3;
				if (type.vecsize == 4)            return ShaderUniformType::Vec4;
				break;
			}
			CORE_ASSERT(false, "Unknown type!");
			return ShaderUniformType::None;
		}

		static VkShaderStageFlagBits ShaderTypeFromString(const std::string& type)
		{
			if (type == "vertex" || type == "vert")                      return VK_SHADER_STAGE_VERTEX_BIT;
			if (type == "fragment" || type == "pixel" || type == "frag")  return VK_SHADER_STAGE_FRAGMENT_BIT;
			if (type == "compute" || type == "comp")                     return VK_SHADER_STAGE_COMPUTE_BIT;
			if (type == "rayGen" || type == "raygen" || type == "rgen")  return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			if (type == "closestHit" || type == "closesthit" || type == "rchit") return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			if (type == "anyHit" || type == "anyhit" || type == "rahit") return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
			if (type == "missHit" || type == "miss" || type == "rmiss")   return VK_SHADER_STAGE_MISS_BIT_KHR;
			if (type == "intersection" || type == "rint")                return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

			return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}

		static VkShaderStageFlagBits ShaderStageFromFileExtension(const std::string& filepath)
		{
			const std::filesystem::path path(filepath);
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

			if (extension == ".vert")  return VK_SHADER_STAGE_VERTEX_BIT;
			if (extension == ".frag")  return VK_SHADER_STAGE_FRAGMENT_BIT;
			if (extension == ".comp")  return VK_SHADER_STAGE_COMPUTE_BIT;
			if (extension == ".rgen")  return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			if (extension == ".rmiss") return VK_SHADER_STAGE_MISS_BIT_KHR;
			if (extension == ".rchit") return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			if (extension == ".rahit") return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
			if (extension == ".rint")  return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

			return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}

		static std::string ReadShaderFromFile(const std::string& filepath)
		{
			std::filesystem::path resolvedPath = FileSystem::ResolvePath(filepath);
			std::string result;
			std::ifstream in(resolvedPath, std::ios::in | std::ios::binary);
			if (in)
			{
				in.seekg(0, std::ios::end);
				result.resize(in.tellg());
				in.seekg(0, std::ios::beg);
				in.read(&result[0], result.size());
			}
			else
			{
				CORE_ASSERT(false, "Could not load shader: {0} resolved to {1}", filepath, resolvedPath.string());
			}
			in.close();
			return result;
		}

		static const char* VkShaderStageCachedFileExtension(VkShaderStageFlagBits stage)
		{
			switch (stage)
			{
			case VK_SHADER_STAGE_VERTEX_BIT:			return ".cached_vulkan.vert";
			case VK_SHADER_STAGE_FRAGMENT_BIT:			return ".cached_vulkan.frag";
			case VK_SHADER_STAGE_COMPUTE_BIT:			return ".cached_vulkan.comp";
			case VK_SHADER_STAGE_RAYGEN_BIT_KHR:		return ".cached_vulkan.rayGen";
			case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:   return ".cached_vulkan.closestHit";
			case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:		return ".cached_vulkan.anyHit";
			case VK_SHADER_STAGE_MISS_BIT_KHR:			return ".cached_vulkan.missHit";
			case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:  return ".cached_vulkan.intersection";
			}
			CORE_ASSERT(false, "");
			return "";
		}

		static shaderc_shader_kind VkShaderStageToShaderC(VkShaderStageFlagBits stage)
		{
			switch (stage)
			{
			case VK_SHADER_STAGE_VERTEX_BIT:			return shaderc_vertex_shader;
			case VK_SHADER_STAGE_FRAGMENT_BIT:			return shaderc_fragment_shader;
			case VK_SHADER_STAGE_COMPUTE_BIT:			return shaderc_compute_shader;
			case VK_SHADER_STAGE_RAYGEN_BIT_KHR:		return shaderc_raygen_shader;
			case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:   return shaderc_closesthit_shader;
			case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:		return shaderc_anyhit_shader;
			case VK_SHADER_STAGE_MISS_BIT_KHR:			return shaderc_miss_shader;
			case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:  return shaderc_intersection_shader;
			}
			CORE_ASSERT(false, "");
			return (shaderc_shader_kind)0;
		}

	}

	static bool s_PrintShaders = true;
	static std::unordered_map<uint32_t, std::unordered_map<uint32_t, Shader::UniformBuffer*>> s_UniformBuffers; // set -> binding point -> buffer
	static std::unordered_map<uint32_t, std::unordered_map<uint32_t, Shader::StorageBuffer*>> s_StorageBuffers; // set -> binding point -> buffer
	static std::unordered_map<uint32_t, std::unordered_map<uint32_t, Shader::AccelerationStructure*>> s_AccelerationStructure; // set -> binding point -> AS

    Shader::Shader(const std::string& path, bool forceCompile)
        : m_AssetPath(FileSystem::ResolvePath(path).string())
    {
        size_t found = path.find_last_of("/\\");
        m_Name = found != std::string::npos ? path.substr(found + 1) : path;
        found = m_Name.find_last_of(".");
        m_Name = found != std::string::npos ? m_Name.substr(0, found) : m_Name;

        Reload(forceCompile);
    }

	Shader::Shader(const ShaderSpecification& specification, bool forceCompile)
		: m_AssetPath(specification.Stages.empty() ? specification.Name : FileSystem::ResolvePath(specification.Stages.front().FilePath).string())
	{
		CORE_ASSERT(!specification.Name.empty(), "ShaderSpecification requires a name");
		CORE_ASSERT(!specification.Stages.empty(), "ShaderSpecification requires at least one stage");

		m_Name = specification.Name;
		for (const ShaderStageSource& stageSource : specification.Stages)
		{
			CORE_ASSERT(stageSource.Stage != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, "Invalid shader stage in ShaderSpecification");
			CORE_ASSERT(!stageSource.FilePath.empty(), "Invalid shader stage file path in ShaderSpecification");
			m_ShaderSourceFilePaths[stageSource.Stage] = FileSystem::ResolvePath(stageSource.FilePath).string();
		}

		Reload(forceCompile);
	}
    Shader::~Shader()
    {
		m_PipelineShaderStageCreateInfos.clear();
		m_DescriptorSetLayouts.clear();
		m_Buffers.clear();
		m_ShaderDescriptorSets.clear();
		m_VulkanPushConstantRanges.clear();
		m_ShaderSource.clear();
		m_Resources.clear();
		m_TypeCounts.clear();
    }

	void Shader::ClearUniformBuffers()
	{
		s_UniformBuffers.clear();
		s_StorageBuffers.clear();
		s_AccelerationStructure.clear();
	}



	void Shader::SubmitResourcesToFree()
	{
		std::vector<VkPipelineShaderStageCreateInfo> pipelineStages = m_PipelineShaderStageCreateInfos;
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts = m_DescriptorSetLayouts;

		m_PipelineShaderStageCreateInfos.clear();
		m_DescriptorSetLayouts.clear();

		// Deferred deletion should only capture native Vulkan resources by value.
		DeferredResourceDeletionQueue::SubmitCurrentFrame([pipelineStages, descriptorSetLayouts]() mutable
		{
			auto vulkanDevice = RendererContext::GetCurrentDevice()->GetVulkanDevice();

			for (VkPipelineShaderStageCreateInfo pipelineStage : pipelineStages)
			{
				if (pipelineStage.module != VK_NULL_HANDLE)
					vkDestroyShaderModule(vulkanDevice, pipelineStage.module, nullptr);
			}
		});
	}

	void Shader::Reload(bool forceCompile)
	{
		Shader* instance = this;
		RenderQueueManager::Submit([instance, forceCompile]() mutable
		{
			// Clear old shader CPU-side reflection/state. Shader modules are released by ShaderLibrary::ReleaseAllShaders().
			instance->m_ShaderDescriptorSets.clear();
			instance->m_Resources.clear();
			instance->m_PipelineShaderStageCreateInfos.clear();
			instance->m_DescriptorSetLayouts.clear();
			instance->m_ShaderSource.clear();
			instance->m_Buffers.clear();
			instance->m_TypeCounts.clear();
			instance->m_ShaderByteCodeData.clear();

			utils::CreateCacheDirectoryIfNeeded();

			if (!instance->m_ShaderSourceFilePaths.empty())
			{
				// Multi-file shader path. Used by RT pipelines and any future split-stage shader.
				for (const auto& [stage, path] : instance->m_ShaderSourceFilePaths)
				{
					std::string source = utils::ReadShaderFromFile(path);
					instance->m_ShaderSource[stage] = instance->PreProcessStageSource(source, stage, path);
				}
			}
			else
			{
				// Backwards-compatible single-file path. Supports old #type blocks and standalone stage files.
				std::string source = utils::ReadShaderFromFile(instance->m_AssetPath);
				instance->m_ShaderSource = instance->PreProcess(source);
			}

			if (s_PrintShaders)
			{
				for (auto [flag, shader] : instance->m_ShaderSource)
				{
					NV_CORE_TRACE("----------");
					NV_CORE_TRACE(shader);
					NV_CORE_TRACE("----------");
				}
			}

			instance->CompileOrGetVulkanBinary(instance->m_ShaderByteCodeData, forceCompile);
			instance->LoadAndCreateShaders(instance->m_ShaderByteCodeData);
			instance->ReflectAllShaderStages(instance->m_ShaderByteCodeData);
			instance->CreateDescriptors();
		});


	}
	const size_t Shader::GetHash() const
	{
		return std::hash<std::string>{}(m_AssetPath);
	}
	void Shader::LoadAndCreateShaders(const std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData)
	{
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		m_PipelineShaderStageCreateInfos.clear();
		for (auto [stage, data] : shaderData)
		{
			CORE_ASSERT(data.size(), "");
			// Shader Modules Create Info
			VkShaderModuleCreateInfo moduleCreateInfo{};
			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.codeSize = data.size() * sizeof(uint32_t);		// Size of code
			moduleCreateInfo.pCode = data.data();							// Pointer to code 

			// Shader Module
			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule));
			// PipelineStage Create Info
			VkPipelineShaderStageCreateInfo& shaderStage = m_PipelineShaderStageCreateInfos.emplace_back();
			shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStage.stage = stage;
			shaderStage.module = shaderModule;
			shaderStage.pName = "main";
		}
	}
	void Shader::CompileOrGetVulkanBinary(std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& outputBinary, bool forceCompile)
	{
		std::filesystem::path cacheDirectory = utils::GetCacheDirectory();
		for (auto [stage, source] : m_ShaderSource)
		{
			const std::string shaderSourcePath = m_ShaderSourceFilePaths.find(stage) != m_ShaderSourceFilePaths.end()
				? m_ShaderSourceFilePaths.at(stage)
				: m_AssetPath;
			auto extension = utils::VkShaderStageCachedFileExtension(stage);
			// Get the compile shader from the cache directory
			if (!forceCompile)
			{
				std::filesystem::path p = shaderSourcePath;
				auto path = cacheDirectory / (p.filename().string() + extension);
				std::string cachedFilePath = path.string();

				FILE* f;
				// Open the file for reading (r) in binary format (b)
				errno_t err = fopen_s(&f, cachedFilePath.c_str(), "rb");
				if (!err)
				{
					// Point to the end of the file
					fseek(f, 0, SEEK_END);
					// Get the size
					uint64_t size = ftell(f);
					// Set again to the begining
					fseek(f, 0, SEEK_SET);
					// Initialize the output the array with the data size
					outputBinary[stage] = std::vector<uint32_t>(size / sizeof(uint32_t));
					// Read the file and put the data inside the map
					fread(outputBinary[stage].data(), sizeof(uint32_t), outputBinary[stage].size(), f);
					fclose(f);
				}
			}
			// If not exit any cache file or force compile = true
			if (outputBinary[stage].size() == 0)
			{
				// Do we need to init a compiler for each stage?
				shaderc::Compiler compiler;
				shaderc::CompileOptions options;
				options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
				options.SetWarningsAsErrors();
				options.SetGenerateDebugInfo();

				const bool optimize = false;
				if (optimize)
					options.SetOptimizationLevel(shaderc_optimization_level_performance);

				// Compile shader
				{
					auto& shaderSource = m_ShaderSource.at(stage);
					shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(shaderSource, utils::VkShaderStageToShaderC(stage), shaderSourcePath.c_str(), options);

					if (module.GetCompilationStatus() != shaderc_compilation_status_success)
					{
						NV_CORE_ERROR(module.GetErrorMessage());
						CORE_ASSERT(false,"");
					}

					const uint8_t* begin = (const uint8_t*)module.cbegin();
					const uint8_t* end = (const uint8_t*)module.cend();
					const ptrdiff_t size = end - begin;

					outputBinary[stage] = std::vector<uint32_t>(module.cbegin(), module.cend());
				}

				// Cache compiled shader
				{
					std::filesystem::path p = m_AssetPath;
					auto path = cacheDirectory / (p.filename().string() + extension);
					std::string cachedFilePath = path.string();

					FILE* f;
					fopen_s(&f, cachedFilePath.c_str(), "wb");
					fwrite(outputBinary[stage].data(), sizeof(uint32_t), outputBinary[stage].size(), f);
					fclose(f);
				}
			}
		}
	}

	void Shader::ReflectAllShaderStages(const std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>>& shaderData)
	{
		for (auto [stage, data] : shaderData)
			Reflect(stage, data);
	}

	void Shader::Reflect(VkShaderStageFlagBits shaderStage, const std::vector<uint32_t>& shaderData)
	{
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		NV_CORE_TRACE("===========================");
		NV_CORE_TRACE(" Shader Reflection");
		NV_CORE_TRACE(" {0}", m_AssetPath);
		NV_CORE_TRACE("===========================");

		spirv_cross::Compiler compiler(shaderData);
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		NV_CORE_TRACE("Uniform Buffers:");
		for (const auto& resource : resources.uniform_buffers)
		{
			const auto& name = resource.name;
			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t size = (uint32_t)compiler.get_declared_struct_size(bufferType);

			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			auto& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			// If the buffer does not exit add it to the static list
			if (s_UniformBuffers[descriptorSet].find(binding) == s_UniformBuffers[descriptorSet].end())
			{
				UniformBuffer* uniformBuffer = new UniformBuffer();
				uniformBuffer->BindingPoint = binding;
				uniformBuffer->UsageType = BufferUsageType::Static;
				uniformBuffer->Size = size;
				uniformBuffer->Name = name;
				uniformBuffer->ShaderStage = VK_SHADER_STAGE_ALL;
				s_UniformBuffers.at(descriptorSet)[binding] = uniformBuffer;
			}
			// If exit check if this one have a greater size and update the value
			else
			{
				UniformBuffer* uniformBuffer = s_UniformBuffers.at(descriptorSet).at(binding);
				if (size > uniformBuffer->Size)
					uniformBuffer->Size = size;

			}

			shaderDescriptorSet.UniformBuffers[binding] = s_UniformBuffers.at(descriptorSet).at(binding);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("  Size: {0}", size);
			NV_CORE_TRACE("-------------------");
		}
		NV_CORE_TRACE("Storage Buffers:");
		for (const auto& resource : resources.storage_buffers)
		{
			const auto& name = resource.name;
			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t size = (uint32_t)compiler.get_declared_struct_size(bufferType);

			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			ShaderDescriptorSet& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			if (s_StorageBuffers[descriptorSet].find(binding) == s_StorageBuffers[descriptorSet].end())
			{
				Shader::StorageBuffer* storageBuffer = new Shader::StorageBuffer();
				storageBuffer->BindingPoint = binding;
				storageBuffer->Size = size;
				storageBuffer->Name = name;
				storageBuffer->ShaderStage = VK_SHADER_STAGE_ALL;
				s_StorageBuffers.at(descriptorSet)[binding] = storageBuffer;
			}
			else
			{
				Shader::StorageBuffer* storageBuffer = s_StorageBuffers.at(descriptorSet).at(binding);
				if (size > storageBuffer->Size)
					storageBuffer->Size = size;
			}

			shaderDescriptorSet.StorageBuffers[binding] = s_StorageBuffers.at(descriptorSet).at(binding);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("  Size: {0}", size);
			NV_CORE_TRACE("-------------------");
		}

		NV_CORE_TRACE("Acceleration Structures:");
		for (const auto& resource : resources.acceleration_structures)
		{
			const auto& name = resource.name;
			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);

			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			ShaderDescriptorSet& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			if (s_AccelerationStructure[descriptorSet].find(binding) == s_AccelerationStructure[descriptorSet].end())
			{
				Shader::AccelerationStructure* as = new Shader::AccelerationStructure();
				as->BindingPoint = binding;
				as->Name = name;
				as->ShaderStage = VK_SHADER_STAGE_ALL;
				s_AccelerationStructure.at(descriptorSet)[binding] = as;
			}

			shaderDescriptorSet.AccelerationStructures[binding] = s_AccelerationStructure.at(descriptorSet).at(binding);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("-------------------");
		}
		
		NV_CORE_TRACE("Push Constant Buffers:");
		for (const auto& resource : resources.push_constant_buffers)
		{
			const std::string& bufferName = resource.name;
			auto& bufferType = compiler.get_type(resource.base_type_id);
			uint32_t bufferSize = (uint32_t)compiler.get_declared_struct_size(bufferType);
			uint32_t memberCount = uint32_t(bufferType.member_types.size());
			uint32_t bufferOffset = 0;
			if (m_VulkanPushConstantRanges.size())
				bufferOffset = m_VulkanPushConstantRanges.back().offset + m_VulkanPushConstantRanges.back().size;

			VkPushConstantRange& vulkanPushConstantRange = m_VulkanPushConstantRanges.emplace_back();
			vulkanPushConstantRange.stageFlags = shaderStage;
			vulkanPushConstantRange.size = bufferSize - bufferOffset;
			vulkanPushConstantRange.offset = bufferOffset;

			// Skip empty push constant buffers - these are for the renderer only
			if (bufferName.empty() || bufferName == "u_Renderer")
				continue;

			ShaderBuffer& buffer = m_Buffers[bufferName];
			buffer.Name = bufferName;
			buffer.Size = bufferSize - bufferOffset;

			NV_CORE_TRACE("  Name: {0}", bufferName);
			NV_CORE_TRACE("  Member Count: {0}", memberCount);
			NV_CORE_TRACE("  Size: {0}", buffer.Size);

			for (uint32_t i = 0; i < memberCount; i++)
			{
				auto type = compiler.get_type(bufferType.member_types[i]);
				const auto& memberName = compiler.get_member_name(bufferType.self, i);
				auto size = (uint32_t)compiler.get_declared_struct_member_size(bufferType, i);
				auto offset = compiler.type_struct_member_offset(bufferType, i) - bufferOffset;

				std::string uniformName = fmt::format("{}.{}", bufferName, memberName);
				buffer.Uniforms[uniformName] = ShaderUniform(uniformName, utils::SPIRTypeToShaderUniformType(type), size, offset);
			}
		}
		NV_CORE_TRACE("Sampled Images:");
		for (const auto& resource : resources.sampled_images)
		{
			const auto& name = resource.name;
			auto& baseType = compiler.get_type(resource.base_type_id);
			auto& type = compiler.get_type(resource.type_id);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t dimension = baseType.image.dim;
			uint32_t arraySize = type.array[0];
			if (arraySize == 0)
				arraySize = 1;
			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			auto& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			auto& imageSampler = shaderDescriptorSet.ImageSamplers[binding];
			imageSampler.BindingPoint = binding;
			imageSampler.DescriptorSet = descriptorSet;
			imageSampler.Name = name;
			imageSampler.ShaderStage = shaderStage;
			imageSampler.ArraySize = arraySize;

			m_Resources[name] = ShaderResourceDeclaration(name, binding, 1);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
		}

		NV_CORE_TRACE("Storage Images:");
		for (const auto& resource : resources.storage_images)
		{
			const auto& name = resource.name;
			auto& type = compiler.get_type(resource.base_type_id);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t dimension = type.image.dim;

			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			auto& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			auto& imageSampler = shaderDescriptorSet.StorageImages[binding];
			imageSampler.BindingPoint = binding;
			imageSampler.DescriptorSet = descriptorSet;
			imageSampler.Name = name;
			imageSampler.ShaderStage = shaderStage;

			m_Resources[name] = ShaderResourceDeclaration(name, binding, 1);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
		}

		NV_CORE_TRACE("Acceleration Structures:");
		for (const auto& resource : resources.acceleration_structures)
		{
			const auto& name = resource.name;
			auto& type = compiler.get_type(resource.base_type_id);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t descriptorSet = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			uint32_t dimension = type.image.dim;

			if (descriptorSet >= m_ShaderDescriptorSets.size())
				m_ShaderDescriptorSets.resize(descriptorSet + 1);

			auto& shaderDescriptorSet = m_ShaderDescriptorSets[descriptorSet];
			auto& as = *shaderDescriptorSet.AccelerationStructures[binding];
			as.BindingPoint = binding;
			as.DescriptorSet = descriptorSet;
			as.Name = name;
			as.ShaderStage = shaderStage;

			m_Resources[name] = ShaderResourceDeclaration(name, binding, 1);

			NV_CORE_TRACE("  {0} ({1}, {2})", name, descriptorSet, binding);
		}
		
		NV_CORE_TRACE("===========================");


	}

	void Shader::CreateDescriptors()
	{
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();

		//////////////////////////////////////////////////////////////////////
		// Descriptor Pool
		//////////////////////////////////////////////////////////////////////

		m_TypeCounts.clear();
		for (uint32_t set = 0; set < m_ShaderDescriptorSets.size(); set++)
		{
			auto& shaderDescriptorSet = m_ShaderDescriptorSets[set];
			//////////////////////////////////////////////////////////////////////
			// Descriptor Pool Size
			//////////////////////////////////////////////////////////////////////
			// NOTE: Type of descriptors + how many DESCRIPTORS,  
			// not Descriptor Sets (combined makes the pool size)
			//////////////////////////////////////////////////////////////////////
			if (shaderDescriptorSet.UniformBuffers.size())
			{
				uint32_t staticCount = 0, dynamicCount = 0;
				for (auto [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers)
				{
					switch (uniformBuffer->UsageType)
					{
						case BufferUsageType::Static:  staticCount++;
						case BufferUsageType::Dynamic: dynamicCount++;
					}
				}
				if (staticCount > 0)
				{
					VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
					typeCount.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					// Descriptor is the individual piece of data inside a descriptor
					typeCount.descriptorCount = staticCount;
				}
				if (dynamicCount > 0)
				{
					VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
					typeCount.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					// Descriptor is the individual piece of data inside a descriptor
					typeCount.descriptorCount = dynamicCount;
				}
				
			}
			if (shaderDescriptorSet.StorageBuffers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.StorageBuffers.size());
			}
			if (shaderDescriptorSet.ImageSamplers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.ImageSamplers.size());
			}
			if (shaderDescriptorSet.StorageImages.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.StorageImages.size());
			}

			//////////////////////////////////////////////////////////////////////
			// Descriptor Set Layout
			//////////////////////////////////////////////////////////////////////
			std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
			for (auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers)
			{
				VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = (uniformBuffer->UsageType == BufferUsageType::Static) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = uniformBuffer->ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[uniformBuffer->Name];
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = 1;
				set.dstBinding = layoutBinding.binding;
			}
			for (auto& [binding, strorageBuffer] : shaderDescriptorSet.StorageBuffers)
			{
				VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = strorageBuffer->ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[strorageBuffer->Name];
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = 1;
				set.dstBinding = layoutBinding.binding;
			}
			for (auto& [binding, accelerationStructure] : shaderDescriptorSet.AccelerationStructures)
			{
				VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = accelerationStructure->ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[accelerationStructure->Name];
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = 1;
				set.dstBinding = layoutBinding.binding;
			}
			for (auto& [binding, imageSampler] : shaderDescriptorSet.ImageSamplers)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				layoutBinding.descriptorCount = imageSampler.ArraySize;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");

				VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = imageSampler.ArraySize;
				set.dstBinding = layoutBinding.binding;
			}

			for (auto& [bindingAndSet, imageSampler] : shaderDescriptorSet.StorageImages)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;

				uint32_t binding = bindingAndSet & 0xffffffff;
				layoutBinding.binding = binding;

				CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");
				CORE_ASSERT(shaderDescriptorSet.ImageSamplers.find(binding) == shaderDescriptorSet.ImageSamplers.end(), "Binding is already present!");

				VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = 1;
				set.dstBinding = layoutBinding.binding;
			}

			VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
			descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayout.pNext = nullptr;
			descriptorLayout.bindingCount = (uint32_t)(layoutBindings.size());
			descriptorLayout.pBindings = layoutBindings.data();
			
			NV_CORE_INFO("Creating descriptor set {0} with {0} acceleration structures, {1} ubo's, {2} ssbo's, {3} samplers and {4} storage images", 
				set,
				shaderDescriptorSet.AccelerationStructures.size(),
				shaderDescriptorSet.UniformBuffers.size(),
				shaderDescriptorSet.StorageBuffers.size(),
				shaderDescriptorSet.ImageSamplers.size(),
				shaderDescriptorSet.StorageImages.size());
			if (set >= m_DescriptorSetLayouts.size())
				m_DescriptorSetLayouts.resize((size_t)(set + 1));

			m_DescriptorSetLayouts[set] = DescriptorManager::CreateDescriptorSetLayout(&descriptorLayout);
		}
	}

	std::string Shader::PreProcessStageSource(const std::string& source, VkShaderStageFlagBits stage, const std::string& sourcePath)
	{
		shaderc_util::FileFinder fileFinder;

		std::filesystem::path shaderPath = sourcePath;
		if (shaderPath.has_parent_path())
			fileFinder.search_path().emplace_back(shaderPath.parent_path().string());

		// Keep old include folders working, but also allow project-relative, engine-relative,
		// and shader-relative includes. Project folders are searched before engine folders
		// to preserve legacy editor behavior.
		fileFinder.search_path().emplace_back(FileSystem::GetProjectShaderDirectory().string());
		fileFinder.search_path().emplace_back((FileSystem::GetProjectShaderDirectory() / "Include").string());
		fileFinder.search_path().emplace_back((FileSystem::GetProjectShaderDirectory() / "Include" / "Common").string());
		fileFinder.search_path().emplace_back(FileSystem::GetEngineShaderDirectory().string());
		fileFinder.search_path().emplace_back((FileSystem::GetEngineShaderDirectory() / "Include").string());
		fileFinder.search_path().emplace_back((FileSystem::GetEngineShaderDirectory() / "Include" / "Common").string());
		fileFinder.search_path().emplace_back("Resources/Shaders/");
		fileFinder.search_path().emplace_back("Resources/Shaders/Include/");
		fileFinder.search_path().emplace_back("Resources/Shaders/Include/Common/");
		fileFinder.search_path().emplace_back(".");

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

		// Deleted by shaderc. Created per stage so #pragma once/include tracking is stage-local.
		auto includer = std::make_unique<GLSLIncludeProcessor>(&fileFinder);
		options.SetIncluder(std::move(includer));

		const auto preProcessingResult = compiler.PreprocessGlsl(source, utils::VkShaderStageToShaderC(stage), sourcePath.c_str(), options);
		if (preProcessingResult.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			NV_CORE_ERROR(preProcessingResult.GetErrorMessage());
			CORE_ASSERT(false, "Shader preprocessing failed");
		}

		return std::string(preProcessingResult.begin(), preProcessingResult.end());
	}

	std::unordered_map<VkShaderStageFlagBits, std::string> Shader::PreProcess(const std::string& source)
	{
		std::unordered_map<VkShaderStageFlagBits, std::string> shaderSources;

		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = source.find(typeToken, 0);

		// Backwards-compatible standalone stage file path.
		// This keeps compute shaders such as .comp working without requiring #type.
		if (pos == std::string::npos)
		{
			VkShaderStageFlagBits stage = utils::ShaderStageFromFileExtension(m_AssetPath);
			CORE_ASSERT(stage != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, "Shader file has no #type block and its extension does not map to a shader stage");
			m_ShaderSourceFilePaths[stage] = m_AssetPath;
			shaderSources[stage] = PreProcessStageSource(source, stage, m_AssetPath);
			return shaderSources;
		}

		while (pos != std::string::npos)
		{
			size_t eol = source.find_first_of("\r\n", pos);
			CORE_ASSERT(eol != std::string::npos, "Syntax error");
			size_t begin = pos + typeTokenLength + 1;
			std::string type = source.substr(begin, eol - begin);

			auto shaderType = utils::ShaderTypeFromString(type);
			CORE_ASSERT(shaderType != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM, "Invalid shader type specified");

			size_t nextLinePos = source.find_first_not_of("\r\n", eol);
			pos = source.find(typeToken, nextLinePos);

			const size_t sourceStart = nextLinePos == std::string::npos ? source.size() : nextLinePos;
			const size_t sourceEnd = pos == std::string::npos ? source.size() : pos;
			std::string stageSource = source.substr(sourceStart, sourceEnd - sourceStart);

			shaderSources[shaderType] = PreProcessStageSource(stageSource, shaderType, m_AssetPath);
		}

		return shaderSources;
	}


	const VkWriteDescriptorSet* Shader::GetWriteDescriptorSet(const std::string& name, uint32_t set) const
	{
		CORE_ASSERT(set < m_ShaderDescriptorSets.size() && m_ShaderDescriptorSets[set], "");
		if (m_ShaderDescriptorSets[set].WriteDescriptorSets.find(name) == m_ShaderDescriptorSets[set].WriteDescriptorSets.end())
		{
			NV_CORE_WARN("Shader {0} does not contain requested descriptor set {1}", m_Name, name);
			return nullptr;
		}
		return &m_ShaderDescriptorSets[set].WriteDescriptorSets.at(name);
	}


	////////////////////////////////////////////////////////////////
	/// Shader Library
	////////////////////////////////////////////////////////////////
	ShaderLibrary::ShaderLibrary()
	{
	}

	ShaderLibrary::~ShaderLibrary()
	{
	}

	void ShaderLibrary::ReleaseAllShaders()
	{
		SubmitResourceFree();
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader)
	{
		auto& name = shader->GetName();
		CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader not found");
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Load(const std::string& path, bool forceCompile)
	{
		auto shader = CreateRef<Shader>(path, forceCompile);
		auto& name = shader->GetName();
		CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exits");
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Load(const std::string& name, const std::string& path)
	{
		CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exits");
		m_Shaders[name] = CreateRef<Shader>(path);
	}

	void ShaderLibrary::Load(const ShaderSpecification& specification, bool forceCompile)
	{
		CORE_ASSERT(!specification.Name.empty(), "ShaderSpecification requires a name");
		CORE_ASSERT(m_Shaders.find(specification.Name) == m_Shaders.end(), "Shader already exits");
		m_Shaders[specification.Name] = CreateRef<Shader>(specification, forceCompile);
	}

	const Ref<Shader>& ShaderLibrary::Get(const std::string& name) const
	{
		CORE_ASSERT(m_Shaders.find(name) != m_Shaders.end(), "Shader does not exits");
		return m_Shaders.at(name);
	}

	void ShaderLibrary::SubmitResourceFree()
	{
		for (auto [name, shader] : m_Shaders)
			shader->SubmitResourcesToFree();
	}
	////////////////////////////////////////////////////////////////
	/// Shader Uniform
	////////////////////////////////////////////////////////////////
	ShaderUniform::ShaderUniform(std::string name, const ShaderUniformType type, const uint32_t size, const uint32_t offset)
		: m_Name(std::move(name)), m_Type(type), m_Size(size), m_Offset(offset)
	{
	}

	const std::string& ShaderUniform::UniformTypeToString(const ShaderUniformType type)
	{
		if (type == ShaderUniformType::Bool)
		{
			return std::string("Boolean");
		}
		else if (type == ShaderUniformType::Int)
		{
			return std::string("Int");
		}
		else if (type == ShaderUniformType::Float)
		{
			return std::string("Float");
		}

		return std::string("None");
	}

}

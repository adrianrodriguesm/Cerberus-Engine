#include "pch.h"
#include "BindGroup.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RendererContext.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/AccelerationStructure.h"
namespace cerberus
{
	bool BindGroupLayout::BindGroupLayoutInfo::operator==(const BindGroupLayoutInfo& other) const
	{
		if (UniformBuffers.size() != other.UniformBuffers.size() ||
			StorageBuffers.size() != other.StorageBuffers.size() ||
			ImageSamplers.size() != other.ImageSamplers.size()   ||
			ImageStorages.size() != other.ImageStorages.size()   ||
			Set != other.Set)
		{
			return false;
		}

		for (uint32_t i = 0; i < UniformBuffers.size(); i++)
		{
			if (other.UniformBuffers[i].BindingPoint != UniformBuffers[i].BindingPoint)
				return false;
			if (other.UniformBuffers[i].ShaderStage != UniformBuffers[i].ShaderStage)
				return false;
		}

		for (uint32_t i = 0; i < StorageBuffers.size(); i++)
		{
			if (other.StorageBuffers[i].BindingPoint != StorageBuffers[i].BindingPoint)
				return false;
			if (other.StorageBuffers[i].ShaderStage != StorageBuffers[i].ShaderStage)
				return false;
		}

		for (uint32_t i = 0; i < ImageSamplers.size(); i++)
		{
			if (other.ImageSamplers[i].BindingPoint != ImageSamplers[i].BindingPoint)
				return false;
			if (other.ImageSamplers[i].ShaderStage != ImageSamplers[i].ShaderStage)
				return false;
			if (other.ImageSamplers[i].ArraySize != ImageSamplers[i].ArraySize)
				return false;
		}

		for (uint32_t i = 0; i < ImageStorages.size(); i++)
		{
			if (other.ImageStorages[i].BindingPoint != ImageStorages[i].BindingPoint)
				return false;
			if (other.ImageStorages[i].ShaderStage != ImageStorages[i].ShaderStage)
				return false;
			if (other.ImageStorages[i].ArraySize != ImageStorages[i].ArraySize)
				return false;
		}

		return true;
	}
    BindGroupLayout::BindGroupLayout(const BindGroupLayoutInfo& info)
        : m_Info(info)
    {
		const uint32_t framesInFlight = ResourceManager::GetFramesInFlight();
		m_WriteDescriptors.resize(framesInFlight);
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
		// Acceleration Structures
		for (const auto& as : info.AccelerationStructures)
		{
			VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = as.ShaderStage;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.binding = as.BindingPoint;
		}
		// Uniform Buffers
		for (const auto& uniformBuffer : info.UniformBuffers)
		{
			VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = uniformBuffer.ShaderStage;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.binding = uniformBuffer.BindingPoint;
		}
		// Storage Buffers
		for (const auto& storageBuffer : info.StorageBuffers)
		{
			VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = storageBuffer.ShaderStage;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.binding = storageBuffer.BindingPoint;
		}
		// Image Sampler
		for (const auto& imageSampler : info.ImageSamplers)
		{
			VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			layoutBinding.descriptorCount = imageSampler.ArraySize;
			layoutBinding.stageFlags = imageSampler.ShaderStage;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.binding = imageSampler.BindingPoint;
		}
		// Storage Images
		for (const auto& imageStorage : info.ImageStorages)
		{
			auto& layoutBinding = layoutBindings.emplace_back();
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = imageStorage.ShaderStage;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.binding = imageStorage.BindingPoint;
		}
		// Fill VkWriteDescriptorSet
		for (const auto& layoutBinding : layoutBindings)
		{
			for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
			{
				VkWriteDescriptorSet& set = m_WriteDescriptors[frameIndex].emplace_back();
				set = {};
				set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				set.descriptorType = layoutBinding.descriptorType;
				set.descriptorCount = layoutBinding.descriptorCount;
				set.dstBinding = layoutBinding.binding;
			}
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = nullptr;
		layoutInfo.bindingCount = (uint32_t)(layoutBindings.size());
		layoutInfo.pBindings = layoutBindings.data();

		NV_CORE_INFO("Creating descriptor set {0} with {1} ubo's, {2} ssbo's, {3} samplers, {4} storage images, and {5} acceleration structures", 
			info.Set,
			info.UniformBuffers.size(),
			info.StorageBuffers.size(),
			info.ImageSamplers.size(),
			info.ImageStorages.size(),
			info.AccelerationStructures.size());

		// Otherwise create a new one (not found)
		VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout));
    }

    BindGroupLayout::~BindGroupLayout()
    {
		RenderQueueManager::SubmitResourceFree([descriptorLayout = m_DescriptorSetLayout]()
		{
			VkDevice device = RendererContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
		});
		
    }

	BindGroupLayout::BindGroupLayout(BindGroupLayout&& other) noexcept
		: m_Info(std::move(other.m_Info))
		, m_WriteDescriptors(std::move(other.m_WriteDescriptors))
		, m_DescriptorSetLayout(other.m_DescriptorSetLayout)
	{
		other.m_DescriptorSetLayout = VK_NULL_HANDLE;
	}

	BindGroup::BindGroup(const BindGroupSpecification& spec)
		: m_BindGroupLayout(spec.BindGroupLayout), m_Persistent(spec.Persistent)
	{
		BindGroupLayout* layout = ResourceManager::GetBindGroupLayout(m_BindGroupLayout);
		const auto& writeDescriptors = layout->m_WriteDescriptors;

		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		m_WriteDescriptors = writeDescriptors;
		m_Descriptors.resize(framesInFlight);
		/** /
		for (VkWriteDescriptorSet writeDescriptor : writeDescriptors[0])
		{

			std::vector<VkWriteDescriptorSet> entry;
			entry.reserve(framesInFlight);

			for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
				entry.push_back(writeDescriptor);

			m_WriteDescriptors[writeDescriptor.dstBinding] = entry;
		}
		/**/
		if (m_Persistent)
		{
			for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
			{
				m_Descriptors[frameIndex] = ResourceManager::AllocateDescriptorSet(m_BindGroupLayout);

				for (auto& writeDescriptor : m_WriteDescriptors[frameIndex])
				{
					writeDescriptor.dstSet = m_Descriptors[frameIndex];
				}
			}
		}
	}

	BindGroup::~BindGroup()
	{

	}

	void BindGroup::Update()
	{
		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;
		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			Update(frameIndex);
		}
	}

	VkDescriptorSet BindGroup::GetHandle(uint32_t frameIndex)
	{
		CORE_ASSERT(m_Descriptors.size() > frameIndex, "")
		return m_Descriptors[frameIndex];
	}

	void BindGroup::SetUniformBuffer(const Ref<UniformBufferSet> uboSet, uint32_t bindingPoint)
	{
		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;
		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			Ref<UniformBuffer> ubo = uboSet->Get(frameIndex);

			auto& writeDescriptors = m_WriteDescriptors[frameIndex];
			for (auto& writeDescriptor : writeDescriptors)
			{
				if (writeDescriptor.dstBinding == bindingPoint)
				{
					writeDescriptor.pBufferInfo = &ubo->GetBufferDescriptorInfo();
				}
			}
			
		}

	}

	void BindGroup::SetImageView(const Handle<ImageView> imgViewHandle, uint32_t bindingPoint)
	{
		ImageView* imgView = ResourceManager::GetImageView(imgViewHandle);
		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;
		imgView->UpdateDescriptor();
		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			auto& writeDescriptors = m_WriteDescriptors[frameIndex];
			for (auto& writeDescriptor : writeDescriptors)
			{
				if (writeDescriptor.dstBinding == bindingPoint)
				{
					writeDescriptor.pImageInfo = &imgView->GetImageDescriptorInfo();
				}
			}
		}
	}

	void BindGroup::SetAccelerationStructure(const Handle<AccelerationStructure> asHandle, uint32_t bindingPoint)
	{
		AccelerationStructure* accelerationStructure = ResourceManager::GetAccelerationStructure(asHandle);
		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			auto& writeDescriptors = m_WriteDescriptors[frameIndex];
			for (auto& writeDescriptor : writeDescriptors)
			{
				if (writeDescriptor.dstBinding == bindingPoint)
				{
					VkAccelerationStructureKHR vulkanAS = accelerationStructure->GetVulkanAStructure();
					VkWriteDescriptorSetAccelerationStructureKHR asWriteDescriptor{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
					asWriteDescriptor.pAccelerationStructures = &vulkanAS;
					asWriteDescriptor.accelerationStructureCount = 1;
					writeDescriptor.pNext = &asWriteDescriptor;

				}
			}
		}
	}

	void BindGroup::Update(uint32_t frameIndex)
	{
		if (!m_Persistent || m_Descriptors[frameIndex] == VK_NULL_HANDLE)
		{
			m_Descriptors[frameIndex] = ResourceManager::AllocateDescriptorSet(m_BindGroupLayout, m_Persistent);

			CORE_ASSERT(m_Descriptors[frameIndex], "Invalid descriptor set created!");
			//m_DescriptorSets[frameIndex] = descriptorSet;
			for (auto& writeDescriptor : m_WriteDescriptors[frameIndex])
			{
				writeDescriptor.dstSet = m_Descriptors[frameIndex];
			}
		}

		VkDevice vulkanDevice = RendererContext::GetCurrentVulkanDevice();
		vkUpdateDescriptorSets(vulkanDevice, (uint32_t)m_WriteDescriptors[frameIndex].size(), m_WriteDescriptors[frameIndex].data(), 0, nullptr);
	}
}
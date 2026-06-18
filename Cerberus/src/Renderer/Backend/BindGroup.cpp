#include "pch.h"
#include "BindGroup.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RendererContext.h"
#include "Renderer/DescriptorManager.h"
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

		// DescriptorManager owns/caches descriptor set layouts.
		m_DescriptorSetLayout = DescriptorManager::CreateDescriptorSetLayout(&layoutInfo);
    }

    BindGroupLayout::~BindGroupLayout()
    {
		// Descriptor set layouts are cached and owned by DescriptorManager.
		// Individual BindGroupLayout wrappers do not destroy the native layout.
		m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

	BindGroupLayout::BindGroupLayout(BindGroupLayout&& other) noexcept
		: m_Info(std::move(other.m_Info))
		, m_WriteDescriptors(std::move(other.m_WriteDescriptors))
		, m_DescriptorSetLayout(other.m_DescriptorSetLayout)
	{
		other.m_DescriptorSetLayout = VK_NULL_HANDLE;
	}

	BindGroup::BindGroup(const BindGroupSpecification& spec)
		: m_BindGroupLayout(spec.BindGroupLayout)
		, m_Lifetime(spec.Lifetime)
	{
		BindGroupLayout* layout = ResourceManager::GetBindGroupLayout(m_BindGroupLayout);
		const auto& writeDescriptors = layout->m_WriteDescriptors;

		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		m_WriteDescriptors = writeDescriptors;
		m_Descriptors.resize(framesInFlight);

		m_AccelerationStructureDescriptorStates.resize(framesInFlight);

		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			m_AccelerationStructureDescriptorStates[frameIndex].resize(m_WriteDescriptors[frameIndex].size());

			for (auto& state : m_AccelerationStructureDescriptorStates[frameIndex])
			{
				state.AccelerationStructure = VK_NULL_HANDLE;
				state.WriteInfo = {};
				state.WriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
				state.WriteInfo.pNext = nullptr;
				state.WriteInfo.accelerationStructureCount = 1;
				state.WriteInfo.pAccelerationStructures = &state.AccelerationStructure;
			}
		}

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
		if (m_Lifetime == DescriptorAllocationLifetime::Persistent)
		{
			for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
			{
				m_Descriptors[frameIndex] = ResourceManager::AllocateDescriptorSet(m_BindGroupLayout, DescriptorAllocationLifetime::Persistent);

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
		CORE_ASSERT(uboSet, "BindGroup::SetUniformBuffer received null UniformBufferSet");

		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		bool foundBinding = false;

		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			Ref<UniformBuffer> ubo = uboSet->Get(frameIndex);

			CORE_ASSERT(ubo, "BindGroup::SetUniformBuffer received null UniformBuffer");

			auto& writeDescriptors = m_WriteDescriptors[frameIndex];

			for (auto& writeDescriptor : writeDescriptors)
			{
				if (writeDescriptor.dstBinding == bindingPoint)
				{
					CORE_ASSERT(
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						"BindGroup::SetUniformBuffer called on a non-uniform-buffer binding"
					);

					writeDescriptor.pBufferInfo = &ubo->GetBufferDescriptorInfo();
					writeDescriptor.descriptorCount = 1;
					foundBinding = true;
				}
			}
		}

		CORE_ASSERT(foundBinding, "BindGroup::SetUniformBuffer binding point not found in layout");
	}

	void BindGroup::SetImageView(const Handle<ImageView> imgViewHandle, uint32_t bindingPoint)
	{
		ImageView* imgView = ResourceManager::GetImageView(imgViewHandle);

		CORE_ASSERT(imgView, "BindGroup::SetImageView received invalid ImageView handle");

		imgView->UpdateDescriptor();

		const auto& descriptorInfo = imgView->GetImageDescriptorInfo();

		CORE_ASSERT(
			descriptorInfo.imageView != VK_NULL_HANDLE,
			"BindGroup::SetImageView imageView is VK_NULL_HANDLE"
		);

		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		bool foundBinding = false;

		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			auto& writeDescriptors = m_WriteDescriptors[frameIndex];

			for (auto& writeDescriptor : writeDescriptors)
			{
				if (writeDescriptor.dstBinding == bindingPoint)
				{
					CORE_ASSERT(
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
						"BindGroup::SetImageView called on non-image descriptor binding"
					);

					writeDescriptor.pImageInfo = &imgView->GetImageDescriptorInfo();
					foundBinding = true;
				}
			}
		}

		CORE_ASSERT(foundBinding, "BindGroup::SetImageView binding point not found in layout");
	}

	void BindGroup::SetAccelerationStructure(const Handle<AccelerationStructure> asHandle, uint32_t bindingPoint)
	{
		AccelerationStructure* accelerationStructure = ResourceManager::GetAccelerationStructure(asHandle);

		CORE_ASSERT(accelerationStructure, "BindGroup::SetAccelerationStructure received invalid AccelerationStructure handle");

		VkAccelerationStructureKHR vulkanAS = accelerationStructure->GetVulkanAStructure();

		CORE_ASSERT(vulkanAS != VK_NULL_HANDLE, "BindGroup::SetAccelerationStructure received VK_NULL_HANDLE acceleration structure");

		uint32_t framesInFlight = ResourceManager::GetRendererConfig().FramesInFlight;

		bool foundBinding = false;

		for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
		{
			auto& writeDescriptors = m_WriteDescriptors[frameIndex];
			auto& asStates = m_AccelerationStructureDescriptorStates[frameIndex];

			CORE_ASSERT(writeDescriptors.size() == asStates.size(), "BindGroup AS descriptor state size mismatch");

			for (uint32_t writeIndex = 0; writeIndex < writeDescriptors.size(); writeIndex++)
			{
				VkWriteDescriptorSet& writeDescriptor = writeDescriptors[writeIndex];

				if (writeDescriptor.dstBinding == bindingPoint)
				{
					CORE_ASSERT(
						writeDescriptor.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
						"BindGroup::SetAccelerationStructure called on a non-acceleration-structure binding"
					);

					AccelerationStructureDescriptorState& state = asStates[writeIndex];

					state.AccelerationStructure = vulkanAS;

					state.WriteInfo = {};
					state.WriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
					state.WriteInfo.pNext = nullptr;
					state.WriteInfo.accelerationStructureCount = 1;
					state.WriteInfo.pAccelerationStructures = &state.AccelerationStructure;

					writeDescriptor.pNext = &state.WriteInfo;
					writeDescriptor.descriptorCount = 1;

					foundBinding = true;
				}
			}
		}

		CORE_ASSERT(foundBinding, "BindGroup::SetAccelerationStructure binding point not found in layout");
	}

	void BindGroup::Update(uint32_t frameIndex)
	{
		if (m_Lifetime == DescriptorAllocationLifetime::Transient || m_Descriptors[frameIndex] == VK_NULL_HANDLE)
		{
			m_Descriptors[frameIndex] = ResourceManager::AllocateDescriptorSet(m_BindGroupLayout, m_Lifetime);

			CORE_ASSERT(m_Descriptors[frameIndex], "Invalid descriptor set created!");
			//m_DescriptorSets[frameIndex] = descriptorSet;
			for (auto& writeDescriptor : m_WriteDescriptors[frameIndex])
			{
				writeDescriptor.dstSet = m_Descriptors[frameIndex];
			}
		}

		// Validate the write descriptors before updating -> TODO: Add a macro to disable this in release builds
		for (const auto& write : m_WriteDescriptors[frameIndex])
		{
			switch (write.descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			{
				if (!write.pImageInfo || write.pImageInfo->imageView == VK_NULL_HANDLE)
				{
					NV_CORE_ERROR(
						"BindGroup invalid image descriptor. Binding={}, Type={}, Frame={}, pImageInfo={}, imageView={}",
						write.dstBinding,
						(uint32_t)write.descriptorType,
						frameIndex,
						(void*)write.pImageInfo,
						write.pImageInfo ? (void*)write.pImageInfo->imageView : nullptr
					);

					CORE_ASSERT(false, "BindGroup tried to update image descriptor with null imageView");
				}

				break;
			}

			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			{
				if (!write.pBufferInfo || write.pBufferInfo->buffer == VK_NULL_HANDLE)
				{
					NV_CORE_ERROR(
						"BindGroup invalid buffer descriptor. Binding={}, Type={}, Frame={}, pBufferInfo={}",
						write.dstBinding,
						(uint32_t)write.descriptorType,
						frameIndex,
						(void*)write.pBufferInfo
					);

					CORE_ASSERT(false, "BindGroup tried to update buffer descriptor with null buffer");
				}

				break;
			}

			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			{
				CORE_ASSERT(write.pNext, "BindGroup tried to update AS descriptor with null pNext");

				const auto* asInfo = reinterpret_cast<const VkWriteDescriptorSetAccelerationStructureKHR*>(write.pNext);

				CORE_ASSERT(
					asInfo->sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
					"Invalid AS descriptor pNext sType"
				);

				CORE_ASSERT(
					asInfo->accelerationStructureCount > 0,
					"AS descriptor has zero accelerationStructureCount"
				);

				CORE_ASSERT(
					asInfo->pAccelerationStructures,
					"AS descriptor has null pAccelerationStructures"
				);

				CORE_ASSERT(
					asInfo->pAccelerationStructures[0] != VK_NULL_HANDLE,
					"AS descriptor contains VK_NULL_HANDLE acceleration structure"
				);

				break;
			}

			default:
				break;
			}
		}

		VkDevice vulkanDevice = RendererContext::GetCurrentVulkanDevice();
		vkUpdateDescriptorSets(vulkanDevice, (uint32_t)m_WriteDescriptors[frameIndex].size(), m_WriteDescriptors[frameIndex].data(), 0, nullptr);
	}
}
#include "pch.h"
#include "EnvironmentMapBaker.h"

#include "Renderer/ComputeCommandList.h"
#include "Renderer/RenderQueueManager.h"
#include "Renderer/RendererResources.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/Resources/BindGroup.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/TextureAsset.h"
#include "Renderer/TransientCommandExecutor.h"
#include "Renderer/TransferCommandList.h"
#include "Scene/EnvironmentMap.h"
#include "Utils/StringUtils.h"

namespace cerberus
{
	namespace
	{
		uint32_t FindImageBinding(
			std::span<const BindGroupLayout::ImageBinding> bindings,
			const std::string& name)
		{
			for (const BindGroupLayout::ImageBinding& binding : bindings)
			{
				if (binding.Name == name)
					return binding.BindingPoint;
			}

			CORE_ASSERT(false, "Environment compute shader binding was not reflected");
			return 0;
		}

		Handle<ImageView> CreateEnvironmentCubeMipView(
			Handle<TextureCube> textureHandle,
			uint32_t mip)
		{
			TextureCube* texture = ResourceManager::GetTextureCube(textureHandle);
			CORE_ASSERT(texture, "Invalid environment TextureCube");
			CORE_ASSERT(mip < texture->GetMipLevelCount(), "Invalid environment cube mip");

			ImageViewProperties viewProperties{};
			viewProperties.Image = texture->GetImage();
			viewProperties.BaseMip = mip;
			viewProperties.MipCount = 1;
			viewProperties.BaseLayer = 0;
			viewProperties.LayerCount = 6;
			viewProperties.ViewType = ImageViewType::ImageCube;
			viewProperties.SamplerSpec.ShaderResource = false;

			Handle<ImageView> imageView = ResourceManager::CreateImageView(viewProperties);
			ImageView* resource = ResourceManager::GetImageView(imageView);
			CORE_ASSERT(resource, "Failed to create environment cube mip view");
			resource->Invalidate();
			resource->UpdateDescriptor();
			return imageView;
		}

		void InsertEnvironmentComputeReadBarrier(
			TransientCommandExecutor& commandExecutor,
			Handle<TextureCube> textureHandle)
		{
			TextureCube* texture = ResourceManager::GetTextureCube(textureHandle);
			CORE_ASSERT(texture, "Invalid environment TextureCube");

			Image* image = ResourceManager::GetImage(texture->GetImage());
			CORE_ASSERT(image, "Environment TextureCube has an invalid image");

			VkImageSubresourceRange range{};
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = texture->GetMipLevelCount();
			range.baseArrayLayer = 0;
			range.layerCount = 6;

			TransientCommandBuffer commandBuffer = commandExecutor.BeginGraphics();
			TransferCommandList transfer(commandBuffer.GetRecordingView());
			image->InsertImageMemoryBarrier(
				transfer,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				range);
			commandBuffer.SubmitAndWait();
		}
	}

	EnvironmentMapBaker::EnvironmentMapBaker(TransientCommandExecutor& commandExecutor)
		: m_CommandExecutor(&commandExecutor)
	{
	}

	Ref<EnvironmentMap> EnvironmentMapBaker::Bake(
		const std::string& filepath,
		const EnvironmentMapBakeSpecification& specification) const
	{
		CORE_ASSERT(m_CommandExecutor, "EnvironmentMapBaker has no command executor");
		CORE_ASSERT(specification.EnvironmentResolution > 0, "Environment resolution must be greater than zero");
		CORE_ASSERT(specification.IrradianceResolution > 0, "Irradiance resolution must be greater than zero");
		CORE_ASSERT(specification.IrradianceSampleCount > 0, "Irradiance sample count must be greater than zero");

		TextureMetadata sourceMetadata;
		sourceMetadata.DebugName = utils::GetFilename(filepath);
		sourceMetadata.FilePath = filepath;
		sourceMetadata.TextureType = TextureType::Texture2D;
		sourceMetadata.SamplerWrap = SamplerWrap::Clamp;
		sourceMetadata.SamplerFilter = SamplerFilter::Linear;
		sourceMetadata.GenerateMips = false;
		sourceMetadata.SRGB = false;

		Ref<TextureAsset> sourceAsset = CreateRef<TextureAsset>(sourceMetadata);
		if (!sourceAsset->Loaded())
		{
			NV_CORE_ERROR("Failed to load environment map source: {0}", filepath);
			return nullptr;
		}

		if (sourceAsset->GetFormat() != ImageFormat::RGBA32F)
		{
			NV_CORE_ERROR("Environment map must be an HDR texture: {0}", filepath);
			return nullptr;
		}

		Handle<Texture2D> equirectangularTexture =
			ResourceManager::CreateTexture2D(sourceAsset, TextureColorSpace::Linear);

		TextureProperties cubeProperties;
		cubeProperties.DebugName = "Environment Map Unfiltered";
		cubeProperties.SamplerWrap = SamplerWrap::Clamp;
		cubeProperties.SamplerFilter = SamplerFilter::Linear;
		cubeProperties.GenerateMips = true;
		cubeProperties.Storage = true;

		Handle<TextureCube> unfilteredMap = ResourceManager::CreateTextureCube(
			ImageFormat::RGBA32F,
			specification.EnvironmentResolution,
			specification.EnvironmentResolution,
			cubeProperties);

		// Equirectangular HDR -> unfiltered cubemap mip 0.
		{
			Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EquirectangularToCubeMap");
			CORE_ASSERT(shader, "EquirectangularToCubeMap compute shader is not loaded");
			BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
			const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "o_CubeMap");
			const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "u_EquirectangularTex");

			Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
			BindGroupSpecification bindGroupSpecification;
			bindGroupSpecification.BindGroupLayout = layout;
			bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
			Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);

			Handle<ImageView> outputMipView = CreateEnvironmentCubeMipView(unfilteredMap, 0);
			Texture2D* inputTexture = ResourceManager::GetTexture2D(equirectangularTexture);
			BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
			CORE_ASSERT(inputTexture && bindGroup, "Failed to prepare environment conversion resources");

			bindGroup->SetImageView(outputMipView, outputBinding);
			bindGroup->SetImageView(inputTexture->GetImageView(), inputBinding);
			bindGroup->Update(0);

			Handle<ComputePipeline> pipeline = ResourceManager::CreateComputePipeline(shader);
			RenderQueueManager::ExecuteRenderQueue();

			TransientCommandBuffer commandBuffer = m_CommandExecutor->BeginCompute();
			ComputeCommandList compute(commandBuffer.GetRecordingView());
			compute.BindPipeline(pipeline);
			compute.BindDescriptorSets(bindGroupHandle);
			const uint32_t groups = (specification.EnvironmentResolution + 31u) / 32u;
			compute.Dispatch(groups, groups, 6);
			commandBuffer.SubmitAndWait();

			ResourceManager::DestroyComputePipeline(pipeline);
			ResourceManager::DestroyBindGroup(bindGroupHandle);
			ResourceManager::DestroyBindGroupLayout(layout);
			ResourceManager::DestroyImageView(outputMipView);
		}

		TextureCube* unfilteredTexture = ResourceManager::GetTextureCube(unfilteredMap);
		CORE_ASSERT(unfilteredTexture, "Failed to create unfiltered environment map");
		unfilteredTexture->GenerateMips(false);

		cubeProperties.DebugName = "Environment Map Filtered";
		Handle<TextureCube> filteredMap = ResourceManager::CreateTextureCube(
			ImageFormat::RGBA32F,
			specification.EnvironmentResolution,
			specification.EnvironmentResolution,
			cubeProperties);

		// GGX-filter every radiance mip.
		{
			Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EnvironmentMipFilter");
			CORE_ASSERT(shader, "EnvironmentMipFilter compute shader is not loaded");
			BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
			const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "outputTexture");
			const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "inputTexture");

			Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
			Handle<ComputePipeline> pipeline = ResourceManager::CreateComputePipeline(shader);
			RenderQueueManager::ExecuteRenderQueue();

			TextureCube* filteredTexture = ResourceManager::GetTextureCube(filteredMap);
			CORE_ASSERT(filteredTexture, "Failed to create filtered environment map");
			const uint32_t mipCount = filteredTexture->GetMipLevelCount();
			const float deltaRoughness = 1.0f / glm::max(static_cast<float>(mipCount) - 1.0f, 1.0f);

			std::vector<Handle<BindGroup>> bindGroups;
			std::vector<Handle<ImageView>> mipViews;
			bindGroups.reserve(mipCount);
			mipViews.reserve(mipCount);

			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				BindGroupSpecification bindGroupSpecification;
				bindGroupSpecification.BindGroupLayout = layout;
				bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
				Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);
				Handle<ImageView> mipView = CreateEnvironmentCubeMipView(filteredMap, mip);

				BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
				CORE_ASSERT(bindGroup, "Failed to create environment filter bind group");
				bindGroup->SetImageView(mipView, outputBinding);
				bindGroup->SetImageView(unfilteredTexture->GetImageView(), inputBinding);
				bindGroup->Update(0);

				bindGroups.push_back(bindGroupHandle);
				mipViews.push_back(mipView);
			}

			TransientCommandBuffer commandBuffer = m_CommandExecutor->BeginCompute();
			ComputeCommandList compute(commandBuffer.GetRecordingView());
			compute.BindPipeline(pipeline);
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				const uint32_t mipSize = glm::max(1u, specification.EnvironmentResolution >> mip);
				const uint32_t groups = glm::max(1u, (mipSize + 31u) / 32u);
				const float roughness = glm::max(static_cast<float>(mip) * deltaRoughness, 0.05f);
				compute.SetPushConstants(AsBytes(roughness));

				compute.BindDescriptorSets(bindGroups[mip]);
				compute.Dispatch(groups, groups, 6);
			}
			commandBuffer.SubmitAndWait();

			ResourceManager::DestroyComputePipeline(pipeline);
			for (Handle<BindGroup> bindGroup : bindGroups)
				ResourceManager::DestroyBindGroup(bindGroup);
			for (Handle<ImageView> mipView : mipViews)
				ResourceManager::DestroyImageView(mipView);
			ResourceManager::DestroyBindGroupLayout(layout);
		}

		InsertEnvironmentComputeReadBarrier(*m_CommandExecutor, filteredMap);

		TextureProperties irradianceProperties = cubeProperties;
		irradianceProperties.DebugName = "Environment Map Irradiance";
		irradianceProperties.GenerateMips = false;
		Handle<TextureCube> irradianceMap = ResourceManager::CreateTextureCube(
			ImageFormat::RGBA32F,
			specification.IrradianceResolution,
			specification.IrradianceResolution,
			irradianceProperties);

		// Diffuse irradiance convolution.
		{
			Ref<Shader> shader = RendererResources::GetShaderLibrary().Get("EnvironmentIrradiance");
			CORE_ASSERT(shader, "EnvironmentIrradiance compute shader is not loaded");
			BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
			const uint32_t outputBinding = FindImageBinding(layoutInfo.ImageStorages, "o_IrradianceMap");
			const uint32_t inputBinding = FindImageBinding(layoutInfo.ImageSamplers, "u_RadianceMap");

			Handle<BindGroupLayout> layout = ResourceManager::CreateBindGroupLayout(layoutInfo);
			BindGroupSpecification bindGroupSpecification;
			bindGroupSpecification.BindGroupLayout = layout;
			bindGroupSpecification.Lifetime = DescriptorAllocationLifetime::Transient;
			Handle<BindGroup> bindGroupHandle = ResourceManager::CreateBindGroup(bindGroupSpecification);

			TextureCube* filteredTexture = ResourceManager::GetTextureCube(filteredMap);
			TextureCube* irradianceTexture = ResourceManager::GetTextureCube(irradianceMap);
			BindGroup* bindGroup = ResourceManager::GetBindGroup(bindGroupHandle);
			CORE_ASSERT(filteredTexture && irradianceTexture && bindGroup, "Failed to prepare irradiance resources");

			bindGroup->SetImageView(irradianceTexture->GetImageView(), outputBinding);
			bindGroup->SetImageView(filteredTexture->GetImageView(), inputBinding);
			bindGroup->Update(0);

			Handle<ComputePipeline> pipeline = ResourceManager::CreateComputePipeline(shader);
			RenderQueueManager::ExecuteRenderQueue();

			TransientCommandBuffer commandBuffer = m_CommandExecutor->BeginCompute();
			ComputeCommandList compute(commandBuffer.GetRecordingView());
			compute.BindPipeline(pipeline);
			compute.SetPushConstants(AsBytes(specification.IrradianceSampleCount));
			compute.BindDescriptorSets(bindGroupHandle);
			const uint32_t groups = (specification.IrradianceResolution + 31u) / 32u;
			compute.Dispatch(groups, groups, 6);
			commandBuffer.SubmitAndWait();

			ResourceManager::DestroyComputePipeline(pipeline);
			ResourceManager::DestroyBindGroup(bindGroupHandle);
			ResourceManager::DestroyBindGroupLayout(layout);
		}

		InsertEnvironmentComputeReadBarrier(*m_CommandExecutor, irradianceMap);
		ResourceManager::DestroyTextureCube(unfilteredMap);

		Ref<EnvironmentMap> environment = CreateRef<EnvironmentMap>(
			equirectangularTexture,
			filteredMap,
			irradianceMap);
		environment->DebugName = utils::GetFilename(filepath);
		return environment;
	}
}

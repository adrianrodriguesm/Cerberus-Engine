#include "pch.h"
#include "PathTracerSceneRenderer.h"
#include "Core/Profiler.h"
#include "Renderer/Core/Renderer.h"
#include "Renderer/Core/RendererResources.h"
#include "Asset/AssetManager.h"
#include "Math/Matrix.h"
#include "Renderer/RHI/ResourceManager.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/Command/RayTracingCommandList.h"
#include "Renderer/RHI/Command/ComputeCommandList.h"
#include "Renderer/RHI/Pipeline/ComputePipeline.h"
#include "ImGui/ImGuiUI.h"
#include "Utils/StringUtils.h"
#include "Renderer/Shader/ShaderCompiler.h"

#include <cmath>
#include <limits>

namespace cerberus
{
	static constexpr bool ENABLE_ACCELERATION_STRUCTURES = true;

	static constexpr uint32_t RT_TLAS_BINDING = 1;
	static constexpr uint32_t RT_CAMERA_BINDING = 2;
	static constexpr uint32_t RT_MATERIALS_BINDING = 3;
	static constexpr uint32_t RT_INSTANCES_BINDING = 4;
	static constexpr uint32_t RT_TEXTURES_BINDING = 5;
	static constexpr uint32_t RT_ENVIRONMENT_BINDING = 6;
	static constexpr uint32_t RT_SKY_LIGHT_BINDING = 7;
	static constexpr uint32_t RT_PATH_TRACER_BINDING = 8;
	static constexpr uint32_t RT_DIRECTIONAL_LIGHTS_BINDING = 9;
	static constexpr uint32_t RT_GBUFFER_NORMAL_DEPTH_BINDING = 10;
	static constexpr uint32_t RT_GBUFFER_POSITION_MESHID_BINDING = 11;
	static constexpr uint32_t RT_MOTION_VECTOR_BINDING = 12;
	static constexpr uint32_t RT_GBUFFER_ALBEDO_BINDING = 13;
	static constexpr uint32_t RT_RADIANCE_BINDING = 14;
	static constexpr uint32_t RT_GBUFFER_MATERIAL_BINDING = 15;
	// Diffuse/specular split (demodulated) + raw emissive, written by the raygen.
	static constexpr uint32_t RT_RADIANCE_DIFFUSE_BINDING = 16;
	static constexpr uint32_t RT_RADIANCE_SPECULAR_BINDING = 17;
	static constexpr uint32_t RT_RADIANCE_EMISSIVE_BINDING = 18;
	// Clamped envBRDF (specular demod factor) for Finalize's exact recombination.
	static constexpr uint32_t RT_GBUFFER_SPECULAR_ALBEDO_BINDING = 19;
	// Primary + specular-virtual hit distances for the specular a-trous reach
	// (Step 8). RG16F.
	static constexpr uint32_t RT_GBUFFER_HIT_DISTANCE_BINDING = 20;
	// Virtual specular reprojection: motion vector + reflection G-buffer. RG16F.
	static constexpr uint32_t RT_MOTION_VECTOR_SPECULAR_BINDING = 21;
	static constexpr uint32_t RT_SPEC_REFL_HISTORY_BINDING = 22;
	// Primary-hit direct-lighting luma for the A-SVGF temporal gradient. R32F.
	static constexpr uint32_t RT_DIRECT_LUMA_BINDING = 23;

	// Local set-0 bindings for the post-process compute shaders
	// (ReferenceAccumulate.glsl / Finalize.glsl).
	static constexpr uint32_t PP_INPUT_BINDING = 0;
	static constexpr uint32_t PP_OUTPUT_BINDING = 1;
	// Finalize.glsl only: G-buffer inputs for the DebugView visualisations.
	static constexpr uint32_t PP_DEBUG_MOTION_BINDING = 2;
	static constexpr uint32_t PP_DEBUG_NORMAL_DEPTH_BINDING = 3;
	static constexpr uint32_t PP_DEBUG_ALBEDO_BINDING = 4;
	static constexpr uint32_t PP_DEBUG_POSITION_MESHID_BINDING = 5;
	static constexpr uint32_t PP_DEBUG_MATERIAL_BINDING = 6;
	// Finalize.glsl only: diffuse/specular/emissive split. Consumed by the lit
	// recombination path and the "split recombined (raw)" debug view.
	static constexpr uint32_t PP_DEBUG_RADIANCE_DIFFUSE_BINDING = 7;
	static constexpr uint32_t PP_DEBUG_RADIANCE_SPECULAR_BINDING = 8;
	static constexpr uint32_t PP_DEBUG_RADIANCE_EMISSIVE_BINDING = 9;
	static constexpr uint32_t PP_DEBUG_SPECULAR_ALBEDO_BINDING = 10;
	// Reprojected specular EMA, for the "spec EMA raw" debug view. (Binding 11
	// retired along with the per-channel temporal/atrous debug views.)
	static constexpr uint32_t PP_DEBUG_SPLIT_TEMPORAL_SPECULAR_BINDING = 12;
	// Final split a-trous outputs, consumed by the lit recombination path.
	static constexpr uint32_t PP_SPLIT_ATROUS_DIFFUSE_BINDING = 13;
	static constexpr uint32_t PP_SPLIT_ATROUS_SPECULAR_BINDING = 14;
	// Both specular-moments ping-pong slots, for the history-length debug view.
	static constexpr uint32_t PP_SPLIT_MOMENTS_SPEC0_BINDING = 15;
	static constexpr uint32_t PP_SPLIT_MOMENTS_SPEC1_BINDING = 16;
	// Both diffuse-moments ping-pong slots, for the diffuse alpha/history view.
	static constexpr uint32_t PP_SPLIT_MOMENTS_DIFF0_BINDING = 17;
	static constexpr uint32_t PP_SPLIT_MOMENTS_DIFF1_BINDING = 18;

	// Local set-0 bindings for SVGFSpatialPreResolve.glsl.
	static constexpr uint32_t SVGF_PR_IN_RADIANCE_DIFFUSE_BINDING = 0;
	static constexpr uint32_t SVGF_PR_IN_RADIANCE_SPECULAR_BINDING = 1;
	static constexpr uint32_t SVGF_PR_NORMAL_DEPTH_BINDING = 2;
	static constexpr uint32_t SVGF_PR_POSITION_MESHID_BINDING = 3;
	static constexpr uint32_t SVGF_PR_OUT_RADIANCE_DIFFUSE_BINDING = 4;
	static constexpr uint32_t SVGF_PR_OUT_RADIANCE_SPECULAR_BINDING = 5;

	// Local set-0 bindings for SVGFTemporalSplit.glsl (dual channel).
	static constexpr uint32_t SVGF_TS_RADIANCE_DIFFUSE_BINDING = 0;
	static constexpr uint32_t SVGF_TS_RADIANCE_SPECULAR_BINDING = 1;
	static constexpr uint32_t SVGF_TS_CURR_NORMAL_DEPTH_BINDING = 2;
	static constexpr uint32_t SVGF_TS_CURR_POSITION_MESHID_BINDING = 3;
	static constexpr uint32_t SVGF_TS_MOTION_BINDING = 4;
	static constexpr uint32_t SVGF_TS_MOTION_SPECULAR_BINDING = 5;
	static constexpr uint32_t SVGF_TS_PREV_COLOR_HISTORY_DIFFUSE_BINDING = 6;
	static constexpr uint32_t SVGF_TS_PREV_MOMENTS_DIFFUSE_BINDING = 7;
	static constexpr uint32_t SVGF_TS_PREV_COLOR_HISTORY_SPECULAR_BINDING = 8;
	static constexpr uint32_t SVGF_TS_PREV_MOMENTS_SPECULAR_BINDING = 9;
	static constexpr uint32_t SVGF_TS_PREV_HISTORY_GBUFFER_BINDING = 10;
	static constexpr uint32_t SVGF_TS_OUT_TEMPORAL_COLOR_DIFFUSE_BINDING = 11;
	static constexpr uint32_t SVGF_TS_OUT_MOMENTS_DIFFUSE_BINDING = 12;
	static constexpr uint32_t SVGF_TS_OUT_TEMPORAL_COLOR_SPECULAR_BINDING = 13;
	static constexpr uint32_t SVGF_TS_OUT_MOMENTS_SPECULAR_BINDING = 14;
	static constexpr uint32_t SVGF_TS_OUT_HISTORY_GBUFFER_BINDING = 15;
	static constexpr uint32_t SVGF_TS_OUT_COLOR_HISTORY_DIFFUSE_BINDING = 16;
	static constexpr uint32_t SVGF_TS_OUT_COLOR_HISTORY_SPECULAR_BINDING = 17;
	static constexpr uint32_t SVGF_TS_MATERIAL_BINDING = 18;
	static constexpr uint32_t SVGF_TS_SPEC_REFL_CURR_BINDING = 19;
	static constexpr uint32_t SVGF_TS_SPEC_REFL_PREV_BINDING = 20;
	static constexpr uint32_t SVGF_TS_OUT_SPEC_REFL_HISTORY_BINDING = 21;
	static constexpr uint32_t SVGF_TS_DIRECT_LUMA_CURR_BINDING = 22;
	static constexpr uint32_t SVGF_TS_DIRECT_LUMA_PREV_BINDING = 23;
	static constexpr uint32_t SVGF_TS_OUT_DIRECT_LUMA_BINDING = 24;

	// Local set-0 bindings for SVGFHistoryFix.glsl.
	static constexpr uint32_t SVGF_HF_OUT_TEMPORAL_COLOR_DIFFUSE_BINDING = 0;
	static constexpr uint32_t SVGF_HF_OUT_TEMPORAL_COLOR_SPECULAR_BINDING = 1;
	static constexpr uint32_t SVGF_HF_IN_COLOR_DIFFUSE_BINDING = 2;
	static constexpr uint32_t SVGF_HF_IN_COLOR_SPECULAR_BINDING = 3;
	static constexpr uint32_t SVGF_HF_MOMENTS_DIFFUSE_BINDING = 4;
	static constexpr uint32_t SVGF_HF_MOMENTS_SPECULAR_BINDING = 5;
	static constexpr uint32_t SVGF_HF_NORMAL_DEPTH_BINDING = 6;
	static constexpr uint32_t SVGF_HF_POSITION_MESHID_BINDING = 7;

	// Local set-0 bindings for SVGFAtrousSplit.glsl.
	static constexpr uint32_t SVGF_A_IN_COLOR_BINDING = 0;
	static constexpr uint32_t SVGF_A_NORMAL_DEPTH_BINDING = 1;
	static constexpr uint32_t SVGF_A_POSITION_MESHID_BINDING = 2;
	static constexpr uint32_t SVGF_A_MOMENTS_BINDING = 3;
	static constexpr uint32_t SVGF_A_OUT_COLOR_BINDING = 4;
	static constexpr uint32_t SVGF_A_MATERIAL_BINDING = 5;
	static constexpr uint32_t SVGF_A_HIT_DISTANCE_BINDING = 6;
	// Upper bound on a-trous iterations (bind-group array size); the live count
	// (m_AtrousIterations) may run fewer.
	static constexpr uint32_t SVGF_ATROUS_MAX_ITERATIONS = 5;

	// Bind-group convention: set 0 is persistent/global, set 1 is per-frame,
	// set 2 is per-pass, and set 3 is per-material/draw.
	PathTracerSceneRenderer::PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec)
	{
		m_Scene = scene;

		m_Specification = spec;
		m_ViewportResolution = spec.Resolution;
		if (m_ViewportResolution.x <= 0 || m_ViewportResolution.y <= 0)
			m_ViewportResolution = { 1920, 1080 };

		m_RenderScene = CreateScope<RenderScene>();

		// The path tracer owns only RT resources. Raster rendering is rebuilt as
		// an independent renderer on top of GraphicsCommandList.

		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		ShaderSpecification rtShaderSpec;
		rtShaderSpec.Name = "Raytrace";
		rtShaderSpec.Stages = {
			{ VK_SHADER_STAGE_RAYGEN_BIT_KHR,      "engine://Shaders/RayTracing/Raytrace_rgen.rgen" },
			{ VK_SHADER_STAGE_MISS_BIT_KHR,        "engine://Shaders/RayTracing/Raytrace_rmiss.rmiss" },
			{ VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "engine://Shaders/RayTracing/Raytrace_rchit.rchit" }
		};
		shaderLibrary.Load(rtShaderSpec, true);

		RenderQueueManager::ExecuteRenderQueue();

		// RT bind group layout reflected from Raytrace.rgen set 0.
		{
			Ref<Shader> rtShader = RendererResources::GetShaderLibrary().Get("Raytrace");
			BindGroupLayout::BindGroupLayoutInfo layoutInfoRT = ShaderReflection::ReflectLayoutInfo(rtShader, 0);
			bool foundBindlessTextures = false;
			for (auto& imageSampler : layoutInfoRT.ImageSamplers)
			{
				if (imageSampler.BindingPoint != RT_TEXTURES_BINDING)
					continue;
				imageSampler.ArraySize = m_RenderScene->GetTextureRegistry().GetCapacity();
				imageSampler.Bindless = true;
				foundBindlessTextures = true;
			}
			CORE_ASSERT(foundBindlessTextures, "RT shader reflection did not expose the bindless texture table");
			m_RT_BindGroupLayout = ResourceManager::CreateBindGroupLayout(layoutInfoRT);

			BindGroupSpecification bindGroupRTSpec;
			bindGroupRTSpec.BindGroupLayout = m_RT_BindGroupLayout;
			// This bind group is retained by the SceneRenderer across frames, so its
			// descriptor sets must outlive the per-frame transient-pool reset.
			bindGroupRTSpec.Lifetime = DescriptorAllocationLifetime::Persistent;
			m_RT_BindGroup = ResourceManager::CreateBindGroup(bindGroupRTSpec);
		}

		// RT pipeline.
		{
			PipelineSpecification rtPipelineSpec;
			rtPipelineSpec.DebugName = "RayTrace";
			rtPipelineSpec.Shader = RendererResources::GetShaderLibrary().Get("Raytrace");
			rtPipelineSpec.CullMode = CullMode::Back;
			rtPipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			m_RT_Pipeline = ResourceManager::CreateRayTracingPipeline(rtPipelineSpec);
		}

		// Shader Binding Table. Raygen owns the iterative path loop, while the miss
		// and closest-hit shaders return environment or reconstructed surface data.
		{
			ShaderBindingTableSpecification sbtSpec;
			sbtSpec.RayGenPrograms = { { 0, {} } };
			sbtSpec.MissPrograms = { { 1, {} } };
			sbtSpec.HitPrograms = { { 2, {} } };
			sbtSpec.Pipeline = m_RT_Pipeline;
			m_ShaderBindingTable = CreateScope<ShaderBindingTable>(sbtSpec);
		}

		// Reference-mode post-process compute (accumulate + tone map). Created
		// before the RT images so CreateRTImages() can point their descriptors at
		// the freshly created storage images.
		CreatePostProcessPipelines();

		// Raw radiance + progressive mean + display output storage images.
		CreateRTImages();

		// Camera UBO for RT path.
		{
			UniformBufferSetSpecification uboSetCamSpec;
			uboSetCamSpec.Frames = ResourceManager::GetFramesInFlight();
			uboSetCamSpec.Size = sizeof(CameraUB);
			uboSetCamSpec.BindingPoint = RT_CAMERA_BINDING;

			m_UniformBufferSetCamera = CreateRef<UniformBufferSet>(uboSetCamSpec);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetUniformBuffer(m_UniformBufferSetCamera, RT_CAMERA_BINDING);
		}

		// SkyLight parameters and radiance cubemap. The black renderer resource is
		// always bound first so the descriptor is valid before a Scene supplies an
		// environment asset.
		{
			UniformBufferSetSpecification skyLightBufferSpecification;
			skyLightBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
			skyLightBufferSpecification.Size = sizeof(SkyLightUB);
			skyLightBufferSpecification.BindingPoint = RT_SKY_LIGHT_BINDING;

			m_RT_SkyLightBufferSet = CreateRef<UniformBufferSet>(skyLightBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			TextureCube* fallbackEnvironment = ResourceManager::GetTextureCube(RendererResources::GetBlackCubeTexture());
			CORE_ASSERT(rtBindGroup && fallbackEnvironment, "Failed to initialize RT SkyLight resources");

			rtBindGroup->SetUniformBuffer(m_RT_SkyLightBufferSet, RT_SKY_LIGHT_BINDING);
			rtBindGroup->SetImageView(fallbackEnvironment->GetImageView(), RT_ENVIRONMENT_BINDING);
		}

		// Per-dispatch path tracer state. Parameters.x is the number of samples
		// already represented by the accumulation image.
		{
			UniformBufferSetSpecification pathTracerBufferSpecification;
			pathTracerBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
			pathTracerBufferSpecification.Size = sizeof(PathTracerUB);
			pathTracerBufferSpecification.BindingPoint = RT_PATH_TRACER_BINDING;

			m_RT_PathTracerBufferSet = CreateRef<UniformBufferSet>(pathTracerBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			rtBindGroup->SetUniformBuffer(m_RT_PathTracerBufferSet, RT_PATH_TRACER_BINDING);
		}

		// Directional lights are analytic delta lights. They use a compact UBO;
		// raygen evaluates them explicitly and traces one visibility ray per light.
		{
			UniformBufferSetSpecification directionalLightBufferSpecification;
			directionalLightBufferSpecification.Frames = ResourceManager::GetFramesInFlight();
			directionalLightBufferSpecification.Size = sizeof(DirectionalLightUB);
			directionalLightBufferSpecification.BindingPoint = RT_DIRECTIONAL_LIGHTS_BINDING;

			m_RT_DirectionalLightBufferSet = CreateRef<UniformBufferSet>(directionalLightBufferSpecification);

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
			rtBindGroup->SetUniformBuffer(m_RT_DirectionalLightBufferSet, RT_DIRECTIONAL_LIGHTS_BINDING);
		}

		// GPU material table for RT. The per-instance buffer maps each TLAS hit to
		// the material selected by its mesh/submesh.
		{
			StorageBufferSetSpecification materialBufferSpec;
			materialBufferSpec.Frames = ResourceManager::GetFramesInFlight();
			materialBufferSpec.Size = sizeof(GPUMaterialData);
			materialBufferSpec.BindingPoint = RT_MATERIALS_BINDING;
			materialBufferSpec.BufferUsage = BufferUsageType::Static;

			m_RT_MaterialBufferSet = CreateRef<StorageBufferSet>(materialBufferSpec);

			GPUMaterialData fallbackMaterial{};
			for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); frameIndex++)
			{
				Ref<StorageBuffer> materialBuffer = m_RT_MaterialBufferSet->Get(frameIndex);
				materialBuffer->SetData(&fallbackMaterial, sizeof(GPUMaterialData), 0);
			}

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetStorageBuffer(m_RT_MaterialBufferSet, RT_MATERIALS_BINDING);
		}

		// Per-TLAS-instance data. gl_InstanceCustomIndexEXT indexes this table in
		// closest-hit, which resolves the material selected by the mesh/submesh.
		{
			StorageBufferSetSpecification instanceBufferSpec;
			instanceBufferSpec.Frames = ResourceManager::GetFramesInFlight();
			instanceBufferSpec.Size = sizeof(RayTracingInstanceData);
			instanceBufferSpec.BindingPoint = RT_INSTANCES_BINDING;
			instanceBufferSpec.BufferUsage = BufferUsageType::Static;

			m_RT_InstanceBufferSet = CreateRef<StorageBufferSet>(instanceBufferSpec);

			RayTracingInstanceData fallbackInstance{};
			for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); frameIndex++)
			{
				Ref<StorageBuffer> instanceBuffer = m_RT_InstanceBufferSet->Get(frameIndex);
				instanceBuffer->SetData(&fallbackInstance, sizeof(RayTracingInstanceData), 0);
			}

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetStorageBuffer(m_RT_InstanceBufferSet, RT_INSTANCES_BINDING);
		}

		RenderQueueManager::ExecuteRenderQueue();
	}

	PathTracerSceneRenderer::~PathTracerSceneRenderer()
	{
		// The SBT references the RT pipeline and must be released first.
		m_ShaderBindingTable.reset();
		if (m_RT_BindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_RT_BindGroup);
		if (m_RT_Pipeline.IsValid())
			ResourceManager::DestroyRayTracingPipeline(m_RT_Pipeline);

		if (m_RenderScene)
		{
			m_RenderScene->Shutdown();
			m_RenderScene.reset();
		}

		if (m_RT_BindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_RT_BindGroupLayout);

		ReleasePostProcessPipelines();
		ReleaseRTImages();

		// Raytrace is renderer-specific. Releasing it here guarantees a renderer
		// switch does not retain the inactive pipeline's shader modules.
		RendererResources::GetShaderLibrary().Unload("Raytrace");
		RendererResources::GetShaderLibrary().Unload("ReferenceAccumulate");
		RendererResources::GetShaderLibrary().Unload("Finalize");
		RendererResources::GetShaderLibrary().Unload("SVGFSpatialPreResolve");
		RendererResources::GetShaderLibrary().Unload("SVGFTemporalSplit");
		RendererResources::GetShaderLibrary().Unload("SVGFHistoryFix");
		RendererResources::GetShaderLibrary().Unload("SVGFAtrousSplit");
	}

	void PathTracerSceneRenderer::ResetAccumulation()
	{
		m_AccumulatedFrames = 0;
	}

	bool PathTracerSceneRenderer::HasCameraChanged(const SceneRendererCamera& sceneRendererCamera) const
	{
		if (!m_HasPreviousCamera)
			return true;

		const glm::mat4 projection = sceneRendererCamera.Camera.GetProjection();
		constexpr float epsilon = 0.000001f;

		for (uint32_t column = 0; column < 4; ++column)
		{
			for (uint32_t row = 0; row < 4; ++row)
			{
				if (std::abs(sceneRendererCamera.ViewMatrix[column][row] - m_PreviousView[column][row]) > epsilon ||
					std::abs(projection[column][row] - m_PreviousProjection[column][row]) > epsilon)
				{
					return true;
				}
			}
		}

		return false;
	}

	void PathTracerSceneRenderer::CreatePostProcessPipelines()
	{
		auto& shaderLibrary = RendererResources::GetShaderLibrary();
		shaderLibrary.Load("engine://Shaders/RayTracing/ReferenceAccumulate.glsl", true);
		shaderLibrary.Load("engine://Shaders/RayTracing/Finalize.glsl", true);
		shaderLibrary.Load("engine://Shaders/RayTracing/SVGFSpatialPreResolve.glsl", true);
		shaderLibrary.Load("engine://Shaders/RayTracing/SVGFTemporalSplit.glsl", true);
		shaderLibrary.Load("engine://Shaders/RayTracing/SVGFHistoryFix.glsl", true);
		shaderLibrary.Load("engine://Shaders/RayTracing/SVGFAtrousSplit.glsl", true);
		RenderQueueManager::ExecuteRenderQueue();

		auto reflectLayout = [](const char* shaderName, Handle<BindGroupLayout>& outLayout) -> Ref<Shader>
		{
			Ref<Shader> shader = RendererResources::GetShaderLibrary().Get(shaderName);
			CORE_ASSERT(shader, "Post-process compute shader is not loaded");
			BindGroupLayout::BindGroupLayoutInfo layoutInfo = ShaderReflection::ReflectLayoutInfo(shader, 0);
			outLayout = ResourceManager::CreateBindGroupLayout(layoutInfo);
			return shader;
		};

		auto makeBindGroup = [](Handle<BindGroupLayout> layout) -> Handle<BindGroup>
		{
			BindGroupSpecification bindGroupSpec;
			bindGroupSpec.BindGroupLayout = layout;
			// Retained across frames alongside the RT bind group; only their
			// storage-image descriptors change, and only on image recreation.
			bindGroupSpec.Lifetime = DescriptorAllocationLifetime::Persistent;
			return ResourceManager::CreateBindGroup(bindGroupSpec);
		};

		{
			Ref<Shader> shader = reflectLayout("ReferenceAccumulate", m_ReferenceAccumulateBindGroupLayout);
			m_ReferenceAccumulateBindGroup = makeBindGroup(m_ReferenceAccumulateBindGroupLayout);
			m_ReferenceAccumulatePipeline = ResourceManager::CreateComputePipeline(shader);
		}
		{
			Ref<Shader> shader = reflectLayout("Finalize", m_FinalizeBindGroupLayout);
			m_FinalizeBindGroup = makeBindGroup(m_FinalizeBindGroupLayout);
			m_FinalizePipeline = ResourceManager::CreateComputePipeline(shader);
		}
		{
			Ref<Shader> shader = reflectLayout("SVGFSpatialPreResolve", m_SVGFSpatialPreResolveBindGroupLayout);
			m_SVGFSpatialPreResolveBindGroup = makeBindGroup(m_SVGFSpatialPreResolveBindGroupLayout);
			m_SVGFSpatialPreResolvePipeline = ResourceManager::CreateComputePipeline(shader);
		}
		{
			Ref<Shader> shader = reflectLayout("SVGFTemporalSplit", m_SVGFTemporalSplitBindGroupLayout);
			m_SVGFTemporalSplitBindGroup[0] = makeBindGroup(m_SVGFTemporalSplitBindGroupLayout);
			m_SVGFTemporalSplitBindGroup[1] = makeBindGroup(m_SVGFTemporalSplitBindGroupLayout);
			m_SVGFTemporalSplitPipeline = ResourceManager::CreateComputePipeline(shader);
		}
		{
			Ref<Shader> shader = reflectLayout("SVGFHistoryFix", m_SVGFHistoryFixBindGroupLayout);
			m_SVGFHistoryFixBindGroup[0] = makeBindGroup(m_SVGFHistoryFixBindGroupLayout);
			m_SVGFHistoryFixBindGroup[1] = makeBindGroup(m_SVGFHistoryFixBindGroupLayout);
			m_SVGFHistoryFixPipeline = ResourceManager::CreateComputePipeline(shader);
		}
		{
			// Split a-trous: one pipeline, one bind group per (parity, channel,
			// iteration). Channel is a push-constant field.
			Ref<Shader> shader = reflectLayout("SVGFAtrousSplit", m_SVGFAtrousSplitBindGroupLayout);
			for (uint32_t parity = 0; parity < 2; ++parity)
				for (uint32_t channel = 0; channel < 2; ++channel)
					for (uint32_t iteration = 0; iteration < SVGF_ATROUS_MAX_ITERATIONS; ++iteration)
						m_SVGFAtrousSplitBindGroup[parity][channel][iteration] = makeBindGroup(m_SVGFAtrousSplitBindGroupLayout);
			m_SVGFAtrousSplitPipeline = ResourceManager::CreateComputePipeline(shader);
		}

		RenderQueueManager::ExecuteRenderQueue();
	}

	void PathTracerSceneRenderer::ReleasePostProcessPipelines()
	{
		if (m_ReferenceAccumulatePipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_ReferenceAccumulatePipeline);
		if (m_ReferenceAccumulateBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_ReferenceAccumulateBindGroup);
		if (m_ReferenceAccumulateBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_ReferenceAccumulateBindGroupLayout);
		if (m_FinalizePipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_FinalizePipeline);
		if (m_FinalizeBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_FinalizeBindGroup);
		if (m_FinalizeBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_FinalizeBindGroupLayout);
		if (m_SVGFSpatialPreResolvePipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_SVGFSpatialPreResolvePipeline);
		if (m_SVGFSpatialPreResolveBindGroup.IsValid())
			ResourceManager::DestroyBindGroup(m_SVGFSpatialPreResolveBindGroup);
		if (m_SVGFSpatialPreResolveBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SVGFSpatialPreResolveBindGroupLayout);
		if (m_SVGFTemporalSplitPipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_SVGFTemporalSplitPipeline);
		for (Handle<BindGroup>& bindGroup : m_SVGFTemporalSplitBindGroup)
		{
			if (bindGroup.IsValid())
				ResourceManager::DestroyBindGroup(bindGroup);
			bindGroup = {};
		}
		if (m_SVGFTemporalSplitBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SVGFTemporalSplitBindGroupLayout);
		if (m_SVGFHistoryFixPipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_SVGFHistoryFixPipeline);
		for (Handle<BindGroup>& bindGroup : m_SVGFHistoryFixBindGroup)
		{
			if (bindGroup.IsValid())
				ResourceManager::DestroyBindGroup(bindGroup);
			bindGroup = {};
		}
		if (m_SVGFHistoryFixBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SVGFHistoryFixBindGroupLayout);
		if (m_SVGFAtrousSplitPipeline.IsValid())
			ResourceManager::DestroyComputePipeline(m_SVGFAtrousSplitPipeline);
		for (auto& parityRow : m_SVGFAtrousSplitBindGroup)
			for (auto& channelRow : parityRow)
				for (Handle<BindGroup>& bindGroup : channelRow)
				{
					if (bindGroup.IsValid())
						ResourceManager::DestroyBindGroup(bindGroup);
					bindGroup = {};
				}
		if (m_SVGFAtrousSplitBindGroupLayout.IsValid())
			ResourceManager::DestroyBindGroupLayout(m_SVGFAtrousSplitBindGroupLayout);

		m_ReferenceAccumulatePipeline = {};
		m_ReferenceAccumulateBindGroup = {};
		m_ReferenceAccumulateBindGroupLayout = {};
		m_FinalizePipeline = {};
		m_FinalizeBindGroup = {};
		m_FinalizeBindGroupLayout = {};
		m_SVGFSpatialPreResolvePipeline = {};
		m_SVGFSpatialPreResolveBindGroup = {};
		m_SVGFSpatialPreResolveBindGroupLayout = {};
		m_SVGFTemporalSplitPipeline = {};
		m_SVGFTemporalSplitBindGroupLayout = {};
		m_SVGFHistoryFixPipeline = {};
		m_SVGFHistoryFixBindGroupLayout = {};
		m_SVGFAtrousSplitPipeline = {};
		m_SVGFAtrousSplitBindGroupLayout = {};
	}

	void PathTracerSceneRenderer::CreateRTImages()
	{
		CORE_ASSERT(m_ViewportResolution.x > 0 && m_ViewportResolution.y > 0, "Invalid RT image resolution");

		ImageSpecification outputSpecification;
		outputSpecification.DebugName = "RT Output";
		outputSpecification.Width = static_cast<uint32_t>(m_ViewportResolution.x);
		outputSpecification.Height = static_cast<uint32_t>(m_ViewportResolution.y);
		outputSpecification.Format = ImageFormat::RGBA32F;
		outputSpecification.Usage = ImageUsage::Storage;
		outputSpecification.Flags = SAMPLE_COMPATIBLE;
		outputSpecification.Mips = 1;
		outputSpecification.Layers = 1;

		m_RT_OutputImage = ResourceManager::CreateImage(outputSpecification);
		Image* outputImage = ResourceManager::GetImage(m_RT_OutputImage);
		CORE_ASSERT(outputImage, "Failed to create RT output image");
		outputImage->Invalidate();
		outputImage->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties outputViewProperties;
		outputViewProperties.Image = m_RT_OutputImage;
		outputViewProperties.BaseMip = 0;
		outputViewProperties.MipCount = 1;
		outputViewProperties.BaseLayer = 0;
		outputViewProperties.LayerCount = 1;
		outputViewProperties.ViewType = ImageViewType::Image2D;
		outputViewProperties.SamplerSpec.SamplerWrap = SamplerWrap::Clamp;
		outputViewProperties.SamplerSpec.SamplerFilter = SamplerFilter::Linear;
		outputViewProperties.SamplerSpec.Anisotropy = false;
		outputViewProperties.SamplerSpec.MinLOD = 0.0f;
		outputViewProperties.SamplerSpec.MaxLOD = 0.0f;
		outputViewProperties.SamplerSpec.ShaderResource = true;

		m_RT_OutputImageView = ResourceManager::CreateImageView(outputViewProperties);
		ImageView* outputImageView = ResourceManager::GetImageView(m_RT_OutputImageView);
		CORE_ASSERT(outputImageView, "Failed to create RT output image view");
		outputImageView->Invalidate();
		outputImageView->UpdateDescriptor();

		ImageSpecification accumulationSpecification = outputSpecification;
		accumulationSpecification.DebugName = "RT Accumulation";
		accumulationSpecification.Flags = 0;

		m_RT_AccumulationImage = ResourceManager::CreateImage(accumulationSpecification);
		Image* accumulationImage = ResourceManager::GetImage(m_RT_AccumulationImage);
		CORE_ASSERT(accumulationImage, "Failed to create RT accumulation image");
		accumulationImage->Invalidate();
		accumulationImage->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties accumulationViewProperties = outputViewProperties;
		accumulationViewProperties.Image = m_RT_AccumulationImage;
		accumulationViewProperties.SamplerSpec.ShaderResource = false;

		m_RT_AccumulationImageView = ResourceManager::CreateImageView(accumulationViewProperties);
		ImageView* accumulationImageView = ResourceManager::GetImageView(m_RT_AccumulationImageView);
		CORE_ASSERT(accumulationImageView, "Failed to create RT accumulation image view");
		accumulationImageView->Invalidate();
		accumulationImageView->UpdateDescriptor();

		// Raw per-frame radiance the raygen writes; read by ReferenceAccumulate.
		// Never sampled directly, so it mirrors the accumulation spec (no
		// SAMPLE_COMPATIBLE, non-shader-resource view).
		ImageSpecification radianceSpecification = accumulationSpecification;
		radianceSpecification.DebugName = "RT Radiance";

		m_RT_RadianceImage = ResourceManager::CreateImage(radianceSpecification);
		Image* radianceImage = ResourceManager::GetImage(m_RT_RadianceImage);
		CORE_ASSERT(radianceImage, "Failed to create RT radiance image");
		radianceImage->Invalidate();
		radianceImage->SetLayout(
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

		ImageViewProperties radianceViewProperties = accumulationViewProperties;
		radianceViewProperties.Image = m_RT_RadianceImage;

		m_RT_RadianceImageView = ResourceManager::CreateImageView(radianceViewProperties);
		ImageView* radianceImageView = ResourceManager::GetImageView(m_RT_RadianceImageView);
		CORE_ASSERT(radianceImageView, "Failed to create RT radiance image view");
		radianceImageView->Invalidate();
		radianceImageView->UpdateDescriptor();

		// Primary-hit G-buffer. Shares the output target's spec (storage, GENERAL
		// layout, sample-compatible) so the views stay shader-readable for a later
		// denoise/debug pass; raygen writes them on the first path segment.
		auto createGBufferImage = [&](const char* debugName, ImageFormat format, Handle<Image>& outImage, Handle<ImageView>& outView)
		{
			ImageSpecification gbufferSpecification = outputSpecification;
			gbufferSpecification.DebugName = debugName;
			gbufferSpecification.Format = format;

			outImage = ResourceManager::CreateImage(gbufferSpecification);
			Image* image = ResourceManager::GetImage(outImage);
			CORE_ASSERT(image, "Failed to create RT G-buffer image");
			image->Invalidate();
			image->SetLayout(
				VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

			ImageViewProperties gbufferViewProperties = outputViewProperties;
			gbufferViewProperties.Image = outImage;

			outView = ResourceManager::CreateImageView(gbufferViewProperties);
			ImageView* view = ResourceManager::GetImageView(outView);
			CORE_ASSERT(view, "Failed to create RT G-buffer image view");
			view->Invalidate();
			view->UpdateDescriptor();
		};

		createGBufferImage("RT GBuffer NormalDepth", ImageFormat::RGBA32F, m_RT_GBufferNormalDepthImage, m_RT_GBufferNormalDepthImageView);
		createGBufferImage("RT GBuffer PositionMeshID", ImageFormat::RGBA32F, m_RT_GBufferPositionMeshIDImage, m_RT_GBufferPositionMeshIDImageView);
		createGBufferImage("RT GBuffer Albedo", ImageFormat::RGBA16F, m_RT_GBufferAlbedoImage, m_RT_GBufferAlbedoImageView);
		createGBufferImage("RT Motion Vector", ImageFormat::RG16F, m_RT_MotionVectorImage, m_RT_MotionVectorImageView);
		createGBufferImage("RT GBuffer Material", ImageFormat::RG16F, m_RT_GBufferMaterialImage, m_RT_GBufferMaterialImageView);

		// Diffuse/specular split (demodulated illumination) + raw emissive. RGBA32F
		// to match the denoiser chain that will consume them; the raygen writes all
		// three every frame so the debug views are valid in reference mode too.
		createGBufferImage("RT Radiance Diffuse", ImageFormat::RGBA32F, m_RT_RadianceDiffuseImage, m_RT_RadianceDiffuseImageView);
		createGBufferImage("RT Radiance Specular", ImageFormat::RGBA32F, m_RT_RadianceSpecularImage, m_RT_RadianceSpecularImageView);
		createGBufferImage("RT Radiance Emissive", ImageFormat::RGBA32F, m_RT_RadianceEmissiveImage, m_RT_RadianceEmissiveImageView);
		createGBufferImage("RT GBuffer SpecularAlbedo", ImageFormat::RGBA16F, m_RT_GBufferSpecularAlbedoImage, m_RT_GBufferSpecularAlbedoImageView);
		createGBufferImage("RT GBuffer HitDistance", ImageFormat::RG16F, m_RT_GBufferHitDistanceImage, m_RT_GBufferHitDistanceImageView);
		// Spatially pre-resolved diffuse/specular (SVGFSpatialPreResolve), what
		// SVGFTemporalSplit actually reprojects/accumulates. Same format as the raw
		// radiance above; regenerated fresh every frame, not ping-ponged.
		createGBufferImage("RT Radiance Diffuse Resolved", ImageFormat::RGBA32F, m_RT_RadianceDiffuseResolvedImage, m_RT_RadianceDiffuseResolvedImageView);
		createGBufferImage("RT Radiance Specular Resolved", ImageFormat::RGBA32F, m_RT_RadianceSpecularResolvedImage, m_RT_RadianceSpecularResolvedImageView);
		createGBufferImage("RT Motion Vector Specular", ImageFormat::RG16F, m_RT_MotionVectorSpecularImage, m_RT_MotionVectorSpecularImageView);
		createGBufferImage("RT SpecReflHistory", ImageFormat::RG16F, m_RT_SpecReflHistoryImage, m_RT_SpecReflHistoryImageView);
		for (uint32_t slot = 0; slot < 2; ++slot)
			createGBufferImage(slot == 0 ? "RT SpecReflHistory PP A" : "RT SpecReflHistory PP B", ImageFormat::RG16F,
				m_RT_SpecReflHistoryPPImage[slot], m_RT_SpecReflHistoryPPImageView[slot]);
		createGBufferImage("RT DirectLuma", ImageFormat::RED32F, m_RT_DirectLumaImage, m_RT_DirectLumaImageView);
		// The ping-pong carries the temporally accumulated direct-luma mean (the
		// low-noise reference for the A-SVGF gradient; its band is spatial).
		for (uint32_t slot = 0; slot < 2; ++slot)
			createGBufferImage(slot == 0 ? "RT DirectLuma PP A" : "RT DirectLuma PP B", ImageFormat::RED32F,
				m_RT_DirectLumaPPImage[slot], m_RT_DirectLumaPPImageView[slot]);

		// Packed prev-frame G-buffer (oct normal, linear depth, mesh id) for the
		// reprojection consistency test; shared by both split channels, ping-pong
		// by frame parity.
		for (uint32_t slot = 0; slot < 2; ++slot)
			createGBufferImage(slot == 0 ? "RT HistoryGBuffer A" : "RT HistoryGBuffer B", ImageFormat::RGBA32F,
				m_RT_HistoryGBufferImage[slot], m_RT_HistoryGBufferImageView[slot]);

		// Diffuse/specular split denoiser chains (Step 3): one full set of
		// ColorHistory[2] / Moments[2] / TemporalColor / ATrous[2] per signal.
		// Not wired to any pass yet -- Steps 4-5 do that. HistoryGBuffer is shared
		// with the single-signal chain above. RGBA16F (half the VRAM of the
		// single-signal chain's RGBA32F, standard for SVGF-family denoisers):
		// demodulated illumination, luminance moments and variance all fit its
		// precision, and two full chains at 32F would not fit alongside the rest.
		{
			const char* signalTag[2] = { "Diffuse", "Specular" };
			for (uint32_t signal = 0; signal < 2; ++signal)
			{
				for (uint32_t slot = 0; slot < 2; ++slot)
				{
					const std::string suffix = std::string(signalTag[signal]) + (slot == 0 ? " A" : " B");
					createGBufferImage(("RT Split ColorHistory " + suffix).c_str(), ImageFormat::RGBA16F,
						m_RT_SplitColorHistoryImage[signal][slot], m_RT_SplitColorHistoryImageView[signal][slot]);
					// Moments carry E[luma^2]. Demodulated specular illumination
					// reaches ~300 even after the firefly clamp, so its square
					// (~1e5) overflows RGBA16F (max 65504) -> Inf -> NaN that the
					// ping-pong then spreads permanently. Keep this one at 32F.
					createGBufferImage(("RT Split Moments " + suffix).c_str(), ImageFormat::RGBA32F,
						m_RT_SplitMomentsImage[signal][slot], m_RT_SplitMomentsImageView[signal][slot]);
					createGBufferImage(("RT Split ATrous " + suffix).c_str(), ImageFormat::RGBA16F,
						m_RT_SplitATrousImage[signal][slot], m_RT_SplitATrousImageView[signal][slot]);
				}
				createGBufferImage((std::string("RT Split TemporalColor ") + signalTag[signal]).c_str(), ImageFormat::RGBA16F,
					m_RT_SplitTemporalColorImage[signal], m_RT_SplitTemporalColorImageView[signal]);
			}
		}

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->SetImageView(m_RT_RadianceImageView, RT_RADIANCE_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferNormalDepthImageView, RT_GBUFFER_NORMAL_DEPTH_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferPositionMeshIDImageView, RT_GBUFFER_POSITION_MESHID_BINDING);
		rtBindGroup->SetImageView(m_RT_MotionVectorImageView, RT_MOTION_VECTOR_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferAlbedoImageView, RT_GBUFFER_ALBEDO_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferMaterialImageView, RT_GBUFFER_MATERIAL_BINDING);
		rtBindGroup->SetImageView(m_RT_RadianceDiffuseImageView, RT_RADIANCE_DIFFUSE_BINDING);
		rtBindGroup->SetImageView(m_RT_RadianceSpecularImageView, RT_RADIANCE_SPECULAR_BINDING);
		rtBindGroup->SetImageView(m_RT_RadianceEmissiveImageView, RT_RADIANCE_EMISSIVE_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferSpecularAlbedoImageView, RT_GBUFFER_SPECULAR_ALBEDO_BINDING);
		rtBindGroup->SetImageView(m_RT_GBufferHitDistanceImageView, RT_GBUFFER_HIT_DISTANCE_BINDING);
		rtBindGroup->SetImageView(m_RT_MotionVectorSpecularImageView, RT_MOTION_VECTOR_SPECULAR_BINDING);
		rtBindGroup->SetImageView(m_RT_SpecReflHistoryImageView, RT_SPEC_REFL_HISTORY_BINDING);
		rtBindGroup->SetImageView(m_RT_DirectLumaImageView, RT_DIRECT_LUMA_BINDING);

		// Post-process compute descriptors: raw radiance -> progressive mean ->
		// display output, plus the G-buffer inputs Finalize needs for DebugView.
		// Refreshed here because the storage images are recreated on resize while
		// the bind groups persist.
		BindGroup* accumulateBindGroup = ResourceManager::GetBindGroup(m_ReferenceAccumulateBindGroup);
		BindGroup* finalizeBindGroup = ResourceManager::GetBindGroup(m_FinalizeBindGroup);
		CORE_ASSERT(accumulateBindGroup && finalizeBindGroup, "Invalid post-process bind groups");

		accumulateBindGroup->SetImageView(m_RT_RadianceImageView, PP_INPUT_BINDING);
		accumulateBindGroup->SetImageView(m_RT_AccumulationImageView, PP_OUTPUT_BINDING);
		accumulateBindGroup->Update();

		finalizeBindGroup->SetImageView(m_RT_AccumulationImageView, PP_INPUT_BINDING);
		finalizeBindGroup->SetImageView(m_RT_OutputImageView, PP_OUTPUT_BINDING);
		finalizeBindGroup->SetImageView(m_RT_MotionVectorImageView, PP_DEBUG_MOTION_BINDING);
		finalizeBindGroup->SetImageView(m_RT_GBufferNormalDepthImageView, PP_DEBUG_NORMAL_DEPTH_BINDING);
		finalizeBindGroup->SetImageView(m_RT_GBufferAlbedoImageView, PP_DEBUG_ALBEDO_BINDING);
		finalizeBindGroup->SetImageView(m_RT_GBufferPositionMeshIDImageView, PP_DEBUG_POSITION_MESHID_BINDING);
		finalizeBindGroup->SetImageView(m_RT_GBufferMaterialImageView, PP_DEBUG_MATERIAL_BINDING);
		finalizeBindGroup->SetImageView(m_RT_RadianceDiffuseImageView, PP_DEBUG_RADIANCE_DIFFUSE_BINDING);
		finalizeBindGroup->SetImageView(m_RT_RadianceSpecularImageView, PP_DEBUG_RADIANCE_SPECULAR_BINDING);
		finalizeBindGroup->SetImageView(m_RT_RadianceEmissiveImageView, PP_DEBUG_RADIANCE_EMISSIVE_BINDING);
		finalizeBindGroup->SetImageView(m_RT_GBufferSpecularAlbedoImageView, PP_DEBUG_SPECULAR_ALBEDO_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitTemporalColorImageView[1], PP_DEBUG_SPLIT_TEMPORAL_SPECULAR_BINDING);
		// Split a-trous final output. Initial guess; UpdateUBOData re-points these
		// to SplitATrous[ch][atrousIterations & 1] alongside the Finalize source
		// whenever the iteration count changes.
		finalizeBindGroup->SetImageView(m_RT_SplitATrousImageView[0][0], PP_SPLIT_ATROUS_DIFFUSE_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitATrousImageView[1][0], PP_SPLIT_ATROUS_SPECULAR_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitMomentsImageView[1][0], PP_SPLIT_MOMENTS_SPEC0_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitMomentsImageView[1][1], PP_SPLIT_MOMENTS_SPEC1_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitMomentsImageView[0][0], PP_SPLIT_MOMENTS_DIFF0_BINDING);
		finalizeBindGroup->SetImageView(m_RT_SplitMomentsImageView[0][1], PP_SPLIT_MOMENTS_DIFF1_BINDING);
		finalizeBindGroup->Update();

		// SVGFSpatialPreResolve bind group: reads this frame's raw radiance +
		// G-buffer, writes the resolved radiance SVGFTemporalSplit reprojects. No
		// ping-pong -- both in and out are regenerated fresh every frame.
		{
			BindGroup* bg = ResourceManager::GetBindGroup(m_SVGFSpatialPreResolveBindGroup);
			CORE_ASSERT(bg, "Invalid SVGF spatial pre-resolve bind group");

			bg->SetImageView(m_RT_RadianceDiffuseImageView, SVGF_PR_IN_RADIANCE_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_RadianceSpecularImageView, SVGF_PR_IN_RADIANCE_SPECULAR_BINDING);
			bg->SetImageView(m_RT_GBufferNormalDepthImageView, SVGF_PR_NORMAL_DEPTH_BINDING);
			bg->SetImageView(m_RT_GBufferPositionMeshIDImageView, SVGF_PR_POSITION_MESHID_BINDING);
			bg->SetImageView(m_RT_RadianceDiffuseResolvedImageView, SVGF_PR_OUT_RADIANCE_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_RadianceSpecularResolvedImageView, SVGF_PR_OUT_RADIANCE_SPECULAR_BINDING);
			bg->Update();
		}

		// SVGFTemporalSplit bind group [k]: pre-wired for frame parity k -- reads
		// the ping-pong "prev" slot (1 - k) per signal plus the shared prev
		// HistoryGBuffer, writes the per-signal reprojected EMA + new moments +
		// interim colour history to slot k and the shared HistoryGBuffer to slot k.
		for (uint32_t k = 0; k < 2; ++k)
		{
			const uint32_t prev = 1u - k;
			BindGroup* bg = ResourceManager::GetBindGroup(m_SVGFTemporalSplitBindGroup[k]);
			CORE_ASSERT(bg, "Invalid SVGF temporal split bind group");

			bg->SetImageView(m_RT_RadianceDiffuseResolvedImageView, SVGF_TS_RADIANCE_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_RadianceSpecularResolvedImageView, SVGF_TS_RADIANCE_SPECULAR_BINDING);
			bg->SetImageView(m_RT_GBufferNormalDepthImageView, SVGF_TS_CURR_NORMAL_DEPTH_BINDING);
			bg->SetImageView(m_RT_GBufferPositionMeshIDImageView, SVGF_TS_CURR_POSITION_MESHID_BINDING);
			bg->SetImageView(m_RT_MotionVectorImageView, SVGF_TS_MOTION_BINDING);
			bg->SetImageView(m_RT_MotionVectorSpecularImageView, SVGF_TS_MOTION_SPECULAR_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[0][prev], SVGF_TS_PREV_COLOR_HISTORY_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[0][prev], SVGF_TS_PREV_MOMENTS_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[1][prev], SVGF_TS_PREV_COLOR_HISTORY_SPECULAR_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[1][prev], SVGF_TS_PREV_MOMENTS_SPECULAR_BINDING);
			bg->SetImageView(m_RT_HistoryGBufferImageView[prev], SVGF_TS_PREV_HISTORY_GBUFFER_BINDING);
			bg->SetImageView(m_RT_SplitTemporalColorImageView[0], SVGF_TS_OUT_TEMPORAL_COLOR_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[0][k], SVGF_TS_OUT_MOMENTS_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitTemporalColorImageView[1], SVGF_TS_OUT_TEMPORAL_COLOR_SPECULAR_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[1][k], SVGF_TS_OUT_MOMENTS_SPECULAR_BINDING);
			bg->SetImageView(m_RT_HistoryGBufferImageView[k], SVGF_TS_OUT_HISTORY_GBUFFER_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[0][k], SVGF_TS_OUT_COLOR_HISTORY_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[1][k], SVGF_TS_OUT_COLOR_HISTORY_SPECULAR_BINDING);
			bg->SetImageView(m_RT_GBufferMaterialImageView, SVGF_TS_MATERIAL_BINDING);
			bg->SetImageView(m_RT_SpecReflHistoryImageView, SVGF_TS_SPEC_REFL_CURR_BINDING);
			bg->SetImageView(m_RT_SpecReflHistoryPPImageView[prev], SVGF_TS_SPEC_REFL_PREV_BINDING);
			bg->SetImageView(m_RT_SpecReflHistoryPPImageView[k], SVGF_TS_OUT_SPEC_REFL_HISTORY_BINDING);
			bg->SetImageView(m_RT_DirectLumaImageView, SVGF_TS_DIRECT_LUMA_CURR_BINDING);
			bg->SetImageView(m_RT_DirectLumaPPImageView[prev], SVGF_TS_DIRECT_LUMA_PREV_BINDING);
			bg->SetImageView(m_RT_DirectLumaPPImageView[k], SVGF_TS_OUT_DIRECT_LUMA_BINDING);
			bg->Update();
		}

		// SVGFHistoryFix bind group [k]: reads the temporal result + moments from
		// parity-k slots, writes the fixed result back into SplitTemporalColor
		// (which the a-trous iteration 0 then reads).
		for (uint32_t k = 0; k < 2; ++k)
		{
			BindGroup* bg = ResourceManager::GetBindGroup(m_SVGFHistoryFixBindGroup[k]);
			CORE_ASSERT(bg, "Invalid SVGF history-fix bind group");
			bg->SetImageView(m_RT_SplitTemporalColorImageView[0], SVGF_HF_OUT_TEMPORAL_COLOR_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitTemporalColorImageView[1], SVGF_HF_OUT_TEMPORAL_COLOR_SPECULAR_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[0][k], SVGF_HF_IN_COLOR_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitColorHistoryImageView[1][k], SVGF_HF_IN_COLOR_SPECULAR_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[0][k], SVGF_HF_MOMENTS_DIFFUSE_BINDING);
			bg->SetImageView(m_RT_SplitMomentsImageView[1][k], SVGF_HF_MOMENTS_SPECULAR_BINDING);
			bg->SetImageView(m_RT_GBufferNormalDepthImageView, SVGF_HF_NORMAL_DEPTH_BINDING);
			bg->SetImageView(m_RT_GBufferPositionMeshIDImageView, SVGF_HF_POSITION_MESHID_BINDING);
			bg->Update();
		}

		// Split a-trous bind group [parity][channel][iteration]. Chain shape per
		// signal, moments from that signal's slot:
		//   iter 0 : SplitTemporalColor[ch]  -> SplitColorHistory[ch][parity]
		//   iter 1 : SplitColorHistory[ch]   -> SplitATrous[ch][0]
		//   iter 2 : SplitATrous[ch][0]      -> SplitATrous[ch][1]
		//   iter 3+: ping-pong SplitATrous[ch]
		for (uint32_t parity = 0; parity < 2; ++parity)
		{
			for (uint32_t channel = 0; channel < 2; ++channel)
			{
				const Handle<ImageView> chain[SVGF_ATROUS_MAX_ITERATIONS + 1] = {
					m_RT_SplitTemporalColorImageView[channel],
					m_RT_SplitColorHistoryImageView[channel][parity],
					m_RT_SplitATrousImageView[channel][0], m_RT_SplitATrousImageView[channel][1],
					m_RT_SplitATrousImageView[channel][0], m_RT_SplitATrousImageView[channel][1] };

				for (uint32_t iteration = 0; iteration < SVGF_ATROUS_MAX_ITERATIONS; ++iteration)
				{
					BindGroup* bg = ResourceManager::GetBindGroup(m_SVGFAtrousSplitBindGroup[parity][channel][iteration]);
					CORE_ASSERT(bg, "Invalid SVGF split a-trous bind group");

					bg->SetImageView(chain[iteration], SVGF_A_IN_COLOR_BINDING);
					bg->SetImageView(m_RT_GBufferNormalDepthImageView, SVGF_A_NORMAL_DEPTH_BINDING);
					bg->SetImageView(m_RT_GBufferPositionMeshIDImageView, SVGF_A_POSITION_MESHID_BINDING);
					bg->SetImageView(m_RT_SplitMomentsImageView[channel][parity], SVGF_A_MOMENTS_BINDING);
					bg->SetImageView(chain[iteration + 1], SVGF_A_OUT_COLOR_BINDING);
					bg->SetImageView(m_RT_GBufferMaterialImageView, SVGF_A_MATERIAL_BINDING);
					bg->SetImageView(m_RT_GBufferHitDistanceImageView, SVGF_A_HIT_DISTANCE_BINDING);
					bg->Update();
				}
			}
		}

		// Finalize's source was just (re)wired to RTAccumulation above; force
		// UpdateUBOData to re-point it if the denoiser is currently on.
		m_LastFrameDenoiserEnabled = false;
		m_FinalizeSourceIterations = -1;

		ResetAccumulation();
	}

	void PathTracerSceneRenderer::ReleaseRTImages()
	{
		if (m_RT_OutputImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_OutputImageView);
		if (m_RT_AccumulationImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_AccumulationImageView);
		if (m_RT_RadianceImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_RadianceImageView);
		if (m_RT_GBufferNormalDepthImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_GBufferNormalDepthImageView);
		if (m_RT_GBufferPositionMeshIDImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_GBufferPositionMeshIDImageView);
		if (m_RT_GBufferAlbedoImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_GBufferAlbedoImageView);
		if (m_RT_MotionVectorImageView.IsValid())
			ResourceManager::DestroyImageView(m_RT_MotionVectorImageView);
		if (m_RT_OutputImage.IsValid())
			ResourceManager::DestroyImage(m_RT_OutputImage);
		if (m_RT_AccumulationImage.IsValid())
			ResourceManager::DestroyImage(m_RT_AccumulationImage);
		if (m_RT_RadianceImage.IsValid())
			ResourceManager::DestroyImage(m_RT_RadianceImage);
		if (m_RT_GBufferNormalDepthImage.IsValid())
			ResourceManager::DestroyImage(m_RT_GBufferNormalDepthImage);
		if (m_RT_GBufferPositionMeshIDImage.IsValid())
			ResourceManager::DestroyImage(m_RT_GBufferPositionMeshIDImage);
		if (m_RT_GBufferAlbedoImage.IsValid())
			ResourceManager::DestroyImage(m_RT_GBufferAlbedoImage);
		if (m_RT_MotionVectorImage.IsValid())
			ResourceManager::DestroyImage(m_RT_MotionVectorImage);
		auto destroyImagePair = [](Handle<Image>& image, Handle<ImageView>& view)
		{
			if (view.IsValid())
				ResourceManager::DestroyImageView(view);
			if (image.IsValid())
				ResourceManager::DestroyImage(image);
			image = {};
			view = {};
		};

		for (uint32_t slot = 0; slot < 2; ++slot)
			destroyImagePair(m_RT_HistoryGBufferImage[slot], m_RT_HistoryGBufferImageView[slot]);
		destroyImagePair(m_RT_GBufferMaterialImage, m_RT_GBufferMaterialImageView);
		destroyImagePair(m_RT_RadianceDiffuseImage, m_RT_RadianceDiffuseImageView);
		destroyImagePair(m_RT_RadianceSpecularImage, m_RT_RadianceSpecularImageView);
		destroyImagePair(m_RT_RadianceEmissiveImage, m_RT_RadianceEmissiveImageView);
		destroyImagePair(m_RT_GBufferSpecularAlbedoImage, m_RT_GBufferSpecularAlbedoImageView);
		destroyImagePair(m_RT_GBufferHitDistanceImage, m_RT_GBufferHitDistanceImageView);
		destroyImagePair(m_RT_RadianceDiffuseResolvedImage, m_RT_RadianceDiffuseResolvedImageView);
		destroyImagePair(m_RT_RadianceSpecularResolvedImage, m_RT_RadianceSpecularResolvedImageView);
		destroyImagePair(m_RT_MotionVectorSpecularImage, m_RT_MotionVectorSpecularImageView);
		destroyImagePair(m_RT_SpecReflHistoryImage, m_RT_SpecReflHistoryImageView);
		for (uint32_t slot = 0; slot < 2; ++slot)
			destroyImagePair(m_RT_SpecReflHistoryPPImage[slot], m_RT_SpecReflHistoryPPImageView[slot]);
		destroyImagePair(m_RT_DirectLumaImage, m_RT_DirectLumaImageView);
		for (uint32_t slot = 0; slot < 2; ++slot)
			destroyImagePair(m_RT_DirectLumaPPImage[slot], m_RT_DirectLumaPPImageView[slot]);
		for (uint32_t signal = 0; signal < 2; ++signal)
		{
			for (uint32_t slot = 0; slot < 2; ++slot)
			{
				destroyImagePair(m_RT_SplitColorHistoryImage[signal][slot], m_RT_SplitColorHistoryImageView[signal][slot]);
				destroyImagePair(m_RT_SplitMomentsImage[signal][slot], m_RT_SplitMomentsImageView[signal][slot]);
				destroyImagePair(m_RT_SplitATrousImage[signal][slot], m_RT_SplitATrousImageView[signal][slot]);
			}
			destroyImagePair(m_RT_SplitTemporalColorImage[signal], m_RT_SplitTemporalColorImageView[signal]);
		}

		m_RT_OutputImageView = {};
		m_RT_AccumulationImageView = {};
		m_RT_RadianceImageView = {};
		m_RT_GBufferNormalDepthImageView = {};
		m_RT_GBufferPositionMeshIDImageView = {};
		m_RT_GBufferAlbedoImageView = {};
		m_RT_MotionVectorImageView = {};
		m_RT_OutputImage = {};
		m_RT_AccumulationImage = {};
		m_RT_RadianceImage = {};
		m_RT_GBufferNormalDepthImage = {};
		m_RT_GBufferPositionMeshIDImage = {};
		m_RT_GBufferAlbedoImage = {};
		m_RT_MotionVectorImage = {};
	}



	void PathTracerSceneRenderer::UpdateRTMaterialBuffer()
	{
		CORE_ASSERT(m_RT_MaterialBufferSet, "RT material buffer set is not initialized");

		std::vector<GPUMaterialData> gpuMaterials;

		if (m_RenderScene)
		{
			const auto& materialProxies = m_RenderScene->GetMaterialProxies();
			gpuMaterials.reserve(materialProxies.size());

			for (const MaterialProxy& materialProxy : materialProxies)
			{
				gpuMaterials.push_back(materialProxy.GPUData);
			}
		}

		if (gpuMaterials.empty())
		{
			gpuMaterials.emplace_back();
		}

		const uint32_t uploadSize = static_cast<uint32_t>(gpuMaterials.size() * sizeof(GPUMaterialData));

		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); frameIndex++)
		{
			Ref<StorageBuffer> materialBuffer = m_RT_MaterialBufferSet->Get(frameIndex);
			CORE_ASSERT(materialBuffer, "Invalid RT material storage buffer");
			materialBuffer->SetData(gpuMaterials.data(), uploadSize, 0);
		}
	}


	void PathTracerSceneRenderer::UpdateRTInstanceBuffer()
	{
		CORE_ASSERT(m_RT_InstanceBufferSet, "RT instance buffer set is not initialized");
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");

		const std::vector<RayTracingInstanceData>& rtInstances = m_RenderScene->GetRayTracingInstances();
		CORE_ASSERT(!rtInstances.empty(), "A valid TLAS must have at least one RT instance descriptor");

		const uint32_t uploadSize = static_cast<uint32_t>(rtInstances.size() * sizeof(RayTracingInstanceData));

		for (uint32_t frameIndex = 0; frameIndex < ResourceManager::GetFramesInFlight(); frameIndex++)
		{
			Ref<StorageBuffer> instanceBuffer = m_RT_InstanceBufferSet->Get(frameIndex);
			CORE_ASSERT(instanceBuffer, "Invalid RT instance storage buffer");
			instanceBuffer->SetData(rtInstances.data(), uploadSize, 0);
		}
	}

	void PathTracerSceneRenderer::UpdateRTTextureTable()
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const GlobalTextureRegistry& textureRegistry = m_RenderScene->GetTextureRegistry();
		if (m_TextureRegistryRevision == textureRegistry.GetRevision())
			return;

		std::vector<Handle<ImageView>> textureImageViews;
		const std::span<const Handle<Texture2D>> textures = textureRegistry.GetTextureHandles();
		textureImageViews.reserve(textures.size());

		for (Handle<Texture2D> textureHandle : textures)
		{
			Texture2D* texture = ResourceManager::GetTexture2D(textureHandle);
			CORE_ASSERT(texture, "RenderScene RT texture table contains an invalid Texture2D");
			textureImageViews.push_back(texture->GetImageView());
		}

		Texture2D* fallbackTexture = ResourceManager::GetTexture2D(RendererResources::GetWhiteTexture());
		CORE_ASSERT(fallbackTexture, "Renderer white fallback texture is invalid");

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->SetImageViewArray(textureImageViews, fallbackTexture->GetImageView(), RT_TEXTURES_BINDING);
		m_TextureRegistryRevision = textureRegistry.GetRevision();
	}

	void PathTracerSceneRenderer::UpdateRTEnvironment()
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");

		const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
		const Handle<TextureCube> radianceMapHandle = skyLight.RadianceMap.IsValid()
			? skyLight.RadianceMap
			: RendererResources::GetBlackCubeTexture();

		TextureCube* radianceMap = ResourceManager::GetTextureCube(radianceMapHandle);
		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(radianceMap && rtBindGroup, "Invalid RT environment resources");

		rtBindGroup->SetImageView(radianceMap->GetImageView(), RT_ENVIRONMENT_BINDING);
	}


	void PathTracerSceneRenderer::UpdateUBOData()
	{
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();

		{
			CameraUB cameraUBO;
			cameraUBO.Projection = m_SceneData.SceneRendererCamera.Camera.GetProjection();
			cameraUBO.View = m_SceneData.SceneRendererCamera.ViewMatrix;
			cameraUBO.ViewProjection = cameraUBO.Projection * cameraUBO.View;
			cameraUBO.PreviousViewProjection = m_HasPreviousViewProjection
				? m_PreviousViewProjection
				: cameraUBO.ViewProjection;
			cameraUBO.InverseView = glm::inverse(cameraUBO.View);
			cameraUBO.InverseProjection = glm::inverse(cameraUBO.Projection);
			cameraUBO.InverseViewReverseProjection =
				cameraUBO.InverseView * glm::inverse(m_SceneData.SceneRendererCamera.Camera.GetReverseProjection());
			const glm::vec3 cameraPosition = glm::vec3(cameraUBO.InverseView[3]);

			const glm::vec3 cameraRight =
				glm::normalize(glm::vec3(cameraUBO.InverseView[0]));

			const glm::vec3 cameraUp =
				glm::normalize(glm::vec3(cameraUBO.InverseView[1]));

			const glm::vec3 cameraForward =
				glm::normalize(-glm::vec3(cameraUBO.InverseView[2]));

			// Extract approximate FOV/aspect from projection matrix.
			// abs() protects against projection conventions with negative Y.
			const float tanHalfFovY = 1.0f / std::abs(cameraUBO.Projection[1][1]);
			const float aspect = std::abs(cameraUBO.Projection[1][1] / cameraUBO.Projection[0][0]);

			cameraUBO.Position = glm::vec4(cameraPosition, 1.0f);
			cameraUBO.Forward = glm::vec4(cameraForward, 0.0f);
			cameraUBO.Right = glm::vec4(cameraRight, 0.0f);
			cameraUBO.Up = glm::vec4(cameraUp, 0.0f);
			cameraUBO.Params = glm::vec4(tanHalfFovY, aspect, 0.0f, 0.0f);

			Ref<UniformBuffer> ubo = m_UniformBufferSetCamera->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT camera UBO");

			ubo->SetData(&cameraUBO, sizeof(CameraUB), 0);

			// Cache after upload so the next rendered frame sees this frame's VP.
			m_PreviousViewProjection = cameraUBO.ViewProjection;
			m_HasPreviousViewProjection = true;
		}

		{
			CORE_ASSERT(m_RenderScene && m_RT_SkyLightBufferSet, "RT SkyLight data is not initialized");
			const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();

			SkyLightUB skyLightUBO;
			skyLightUBO.Parameters.x = skyLight.Intensity;
			skyLightUBO.Parameters.y = skyLight.SampleLOD;

			Ref<UniformBuffer> ubo = m_RT_SkyLightBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT SkyLight UBO");
			ubo->SetData(&skyLightUBO, sizeof(SkyLightUB), 0);
		}

		{
			CORE_ASSERT(m_RT_PathTracerBufferSet, "RT path tracer data is not initialized");

			PathTracerUB pathTracerUBO;
			pathTracerUBO.Parameters.x = m_AccumulatedFrames;
			pathTracerUBO.Parameters.y = static_cast<uint32_t>(std::max(m_MaxBounces, 1));
			pathTracerUBO.Parameters.z = m_DenoiserFrameIndex;
			pathTracerUBO.Parameters.w = m_DenoiserEnabled ? 1u : 0u;
			pathTracerUBO.Presentation.x = m_Exposure;
			pathTracerUBO.Presentation.y = m_FireflyClamp; // <= 0 disables (raygen)
			pathTracerUBO.Presentation.z = static_cast<float>(std::max(m_SamplesPerFrame, 1)); // primary paths per pixel per dispatch

			Ref<UniformBuffer> ubo = m_RT_PathTracerBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT path tracer UBO");
			ubo->SetData(&pathTracerUBO, sizeof(PathTracerUB), 0);
		}

		{
			CORE_ASSERT(m_RenderScene && m_RT_DirectionalLightBufferSet, "RT directional light data is not initialized");

			DirectionalLightUB directionalLightUBO;
			directionalLightUBO.Parameters.x = m_RenderScene->GetDirectionalLightCount();
			const auto& directionalLights = m_RenderScene->GetDirectionalLights();
			for (uint32_t lightIndex = 0; lightIndex < m_RenderScene->GetDirectionalLightCount(); ++lightIndex)
			{
				const DirectionalLightProxy& light = directionalLights[lightIndex];
				directionalLightUBO.Lights[lightIndex].DirectionAndIntensity = glm::vec4(light.Direction, light.Intensity);
				directionalLightUBO.Lights[lightIndex].Radiance = glm::vec4(light.Radiance, 0.0f);
			}

			Ref<UniformBuffer> ubo = m_RT_DirectionalLightBufferSet->Get(frameIndex);
			CORE_ASSERT(ubo, "Invalid RT directional light UBO");
			ubo->SetData(&directionalLightUBO, sizeof(DirectionalLightUB), 0);
		}

		BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
		CORE_ASSERT(rtBindGroup, "Invalid RT bind group");
		rtBindGroup->Update(frameIndex);

		// Finalize's HDR source: RTAccumulation in reference mode, otherwise the
		// buffer the last a-trous iteration wrote -- which is ATrous[N & 1] for an
		// iteration count N (see the atrousChain wiring). Re-point it only when
		// the mode or the iteration count actually changes. CreateRTImages leaves
		// it on RTAccumulation with these trackers cleared, so a resize while the
		// denoiser is on still triggers exactly one re-point here.
		const int atrousIterations = glm::clamp(m_AtrousIterations, 2, static_cast<int>(SVGF_ATROUS_MAX_ITERATIONS));
		if (m_DenoiserEnabled != m_LastFrameDenoiserEnabled ||
			(m_DenoiserEnabled && atrousIterations != m_FinalizeSourceIterations))
		{
			BindGroup* finalizeBindGroup = ResourceManager::GetBindGroup(m_FinalizeBindGroup);
			CORE_ASSERT(finalizeBindGroup, "Invalid Finalize bind group");
			// Split a-trous final output per signal = SplitATrous[ch][atrousIterations & 1]
			// (chain idx 2..5 alternate ping/pong). The lit path recombines both;
			// u_Source is only read for reference mode and the "Denoiser variance"
			// debug view, so point it at the specular final in denoiser mode.
			finalizeBindGroup->SetImageView(m_RT_SplitATrousImageView[0][atrousIterations & 1], PP_SPLIT_ATROUS_DIFFUSE_BINDING);
			finalizeBindGroup->SetImageView(m_RT_SplitATrousImageView[1][atrousIterations & 1], PP_SPLIT_ATROUS_SPECULAR_BINDING);
			finalizeBindGroup->SetImageView(
				m_DenoiserEnabled ? m_RT_SplitATrousImageView[1][atrousIterations & 1] : m_RT_AccumulationImageView,
				PP_INPUT_BINDING);
			finalizeBindGroup->Update();
			m_LastFrameDenoiserEnabled = m_DenoiserEnabled;
			m_FinalizeSourceIterations = atrousIterations;
		}
	}

	void PathTracerSceneRenderer::FlushRenderPasses()
	{
		// Descriptor pool reset belongs to Renderer::BeginFrame. Resetting here can invalidate
		// descriptor sets that were already allocated/bound earlier in the same frame.
		uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		UpdateUBOData();

		CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget);
		CORE_ASSERT(commandBuffer, "Invalid main command buffer");

		// Pipeline, all in one command buffer:
		//   PathTrace (ray tracing) -> RTRadiance + G-buffer (normal/depth,
		//                              position/meshid, albedo, motion vector)
		//   then one of:
		//     reference: ReferenceAccumulate -> RTAccumulation (running mean)
		//     denoiser:  SVGFTemporal -> RTTemporalColor (reprojected EMA), then
		//                SVGFAtrous x5: iter 0 writes RTColorHistory[curr] (fed
		//                back next frame), iters 1..4 bounce the ATrous scratch
		//   Finalize -> RTOutput  tone-mapped image, or a DebugView visualisation
		// Every image stays GENERAL across passes and frames; the graph resolves
		// the RT->compute and compute->compute memory dependencies from the
		// declared accesses. ImportImage's initialAccess describes what the
		// previous frame left each image in so the first barrier each frame is
		// correct.
		const bool denoiser = m_DenoiserEnabled;
		const uint32_t curr = m_DenoiserFrameIndex & 1u;
		const uint32_t prev = curr ^ 1u;

		m_RenderGraph.Reset();
		const RGTextureHandle radianceRG = m_RenderGraph.ImportImage(
			m_RT_RadianceImage,
			"RTRadiance",
			RGAccessState{ VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT });
		const RGTextureHandle outputRG = m_RenderGraph.ImportImage(
			m_RT_OutputImage,
			"RTOutput",
			RGAccessState{ VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT });
		// G-buffer: written by the raygen, then read by SVGFTemporal, the a-trous
		// iterations and Finalize. The builder has no read-only storage-image
		// declaration, so every consumer counts as a writer and the graph leaves
		// the previous frame's last access as compute read+write -- describe that
		// so the first barrier (before the raygen re-writes) covers it.
		const RGAccessState gbufferInitialAccess{ VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
		const RGTextureHandle normalDepthRG = m_RenderGraph.ImportImage(
			m_RT_GBufferNormalDepthImage, "RTGBufferNormalDepth", gbufferInitialAccess);
		const RGTextureHandle positionMeshIDRG = m_RenderGraph.ImportImage(
			m_RT_GBufferPositionMeshIDImage, "RTGBufferPositionMeshID", gbufferInitialAccess);
		const RGTextureHandle albedoRG = m_RenderGraph.ImportImage(
			m_RT_GBufferAlbedoImage, "RTGBufferAlbedo", gbufferInitialAccess);
		const RGTextureHandle motionRG = m_RenderGraph.ImportImage(
			m_RT_MotionVectorImage, "RTMotionVector", gbufferInitialAccess);
		const RGTextureHandle materialRG = m_RenderGraph.ImportImage(
			m_RT_GBufferMaterialImage, "RTGBufferMaterial", gbufferInitialAccess);
		// Diffuse/specular split: written by the raygen, read by Finalize (debug
		// views this step; the denoiser chain later). Same access model as the
		// other G-buffers -- every consumer counts as a writer.
		const RGTextureHandle radianceDiffuseRG = m_RenderGraph.ImportImage(
			m_RT_RadianceDiffuseImage, "RTRadianceDiffuse", gbufferInitialAccess);
		const RGTextureHandle radianceSpecularRG = m_RenderGraph.ImportImage(
			m_RT_RadianceSpecularImage, "RTRadianceSpecular", gbufferInitialAccess);
		// Spatially pre-resolved diffuse/specular (SVGFSpatialPreResolve, denoiser
		// mode only): what SVGFTemporalSplit actually reprojects/accumulates.
		const RGTextureHandle radianceDiffuseResolvedRG = m_RenderGraph.ImportImage(
			m_RT_RadianceDiffuseResolvedImage, "RTRadianceDiffuseResolved", gbufferInitialAccess);
		const RGTextureHandle radianceSpecularResolvedRG = m_RenderGraph.ImportImage(
			m_RT_RadianceSpecularResolvedImage, "RTRadianceSpecularResolved", gbufferInitialAccess);
		const RGTextureHandle radianceEmissiveRG = m_RenderGraph.ImportImage(
			m_RT_RadianceEmissiveImage, "RTRadianceEmissive", gbufferInitialAccess);
		const RGTextureHandle specularAlbedoRG = m_RenderGraph.ImportImage(
			m_RT_GBufferSpecularAlbedoImage, "RTGBufferSpecularAlbedo", gbufferInitialAccess);
		// Hit distances for the specular a-trous reach (Step 8.2). Written by the
		// raygen, read by SVGFAtrousSplit.
		const RGTextureHandle hitDistanceRG = m_RenderGraph.ImportImage(
			m_RT_GBufferHitDistanceImage, "RTGBufferHitDistance", gbufferInitialAccess);
		// Virtual specular reprojection: motion vector + reflection G-buffer,
		// written by the raygen, read by SVGFTemporalSplit.
		const RGTextureHandle motionSpecularRG = m_RenderGraph.ImportImage(
			m_RT_MotionVectorSpecularImage, "RTMotionVectorSpecular", gbufferInitialAccess);
		const RGTextureHandle specReflRG = m_RenderGraph.ImportImage(
			m_RT_SpecReflHistoryImage, "RTSpecReflHistory", gbufferInitialAccess);
		// A-SVGF temporal gradient source: raygen writes, SVGFTemporalSplit reads.
		const RGTextureHandle directLumaRG = m_RenderGraph.ImportImage(
			m_RT_DirectLumaImage, "RTDirectLuma", gbufferInitialAccess);
		// Split temporal-pass outputs. Imported unconditionally so the debug views
		// can read them even in reference mode (stale then -- expected); only the
		// SVGFTemporalSplit pass (denoiser mode) writes them.
		const RGTextureHandle splitTemporalDiffRG = m_RenderGraph.ImportImage(
			m_RT_SplitTemporalColorImage[0], "RTSplitTemporalColorDiffuse", gbufferInitialAccess);
		const RGTextureHandle splitTemporalSpecRG = m_RenderGraph.ImportImage(
			m_RT_SplitTemporalColorImage[1], "RTSplitTemporalColorSpecular", gbufferInitialAccess);
		// Both specular- and diffuse-moments ping-pong slots, imported
		// unconditionally for the alpha/history debug views; the denoiser passes
		// below reuse these handles.
		const RGTextureHandle splitMomentsSpec0RG = m_RenderGraph.ImportImage(
			m_RT_SplitMomentsImage[1][0], "RTSplitMomentsSpec0", gbufferInitialAccess);
		const RGTextureHandle splitMomentsSpec1RG = m_RenderGraph.ImportImage(
			m_RT_SplitMomentsImage[1][1], "RTSplitMomentsSpec1", gbufferInitialAccess);
		const RGTextureHandle splitMomentsDiff0RG = m_RenderGraph.ImportImage(
			m_RT_SplitMomentsImage[0][0], "RTSplitMomentsDiff0", gbufferInitialAccess);
		const RGTextureHandle splitMomentsDiff1RG = m_RenderGraph.ImportImage(
			m_RT_SplitMomentsImage[0][1], "RTSplitMomentsDiff1", gbufferInitialAccess);

		const RGAccessState computeRWInitial{ VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
		RGTextureHandle accumulationRG{};
		RGTextureHandle historyGBufferCurrRG{};
		RGTextureHandle historyGBufferPrevRG{};
		RGTextureHandle splitColorHistDiffCurrRG{}, splitColorHistDiffPrevRG{};
		RGTextureHandle splitColorHistSpecCurrRG{}, splitColorHistSpecPrevRG{};
		RGTextureHandle splitMomentsDiffCurrRG{}, splitMomentsDiffPrevRG{};
		RGTextureHandle splitMomentsSpecCurrRG{}, splitMomentsSpecPrevRG{};
		RGTextureHandle splitAtrousDiffPingRG{}, splitAtrousDiffPongRG{};
		RGTextureHandle splitAtrousSpecPingRG{}, splitAtrousSpecPongRG{};
		RGTextureHandle specReflPPCurrRG{}, specReflPPPrevRG{};
		RGTextureHandle directLumaPPCurrRG{}, directLumaPPPrevRG{};
		if (denoiser)
		{
			historyGBufferCurrRG = m_RenderGraph.ImportImage(m_RT_HistoryGBufferImage[curr], "RTHistoryGBufferCurr", computeRWInitial);
			historyGBufferPrevRG = m_RenderGraph.ImportImage(m_RT_HistoryGBufferImage[prev], "RTHistoryGBufferPrev", computeRWInitial);
			specReflPPCurrRG = m_RenderGraph.ImportImage(m_RT_SpecReflHistoryPPImage[curr], "RTSpecReflHistoryPPCurr", computeRWInitial);
			specReflPPPrevRG = m_RenderGraph.ImportImage(m_RT_SpecReflHistoryPPImage[prev], "RTSpecReflHistoryPPPrev", computeRWInitial);
			directLumaPPCurrRG = m_RenderGraph.ImportImage(m_RT_DirectLumaPPImage[curr], "RTDirectLumaPPCurr", computeRWInitial);
			directLumaPPPrevRG = m_RenderGraph.ImportImage(m_RT_DirectLumaPPImage[prev], "RTDirectLumaPPPrev", computeRWInitial);

			splitColorHistDiffCurrRG = m_RenderGraph.ImportImage(m_RT_SplitColorHistoryImage[0][curr], "RTSplitColorHistDiffCurr", computeRWInitial);
			splitColorHistDiffPrevRG = m_RenderGraph.ImportImage(m_RT_SplitColorHistoryImage[0][prev], "RTSplitColorHistDiffPrev", computeRWInitial);
			splitColorHistSpecCurrRG = m_RenderGraph.ImportImage(m_RT_SplitColorHistoryImage[1][curr], "RTSplitColorHistSpecCurr", computeRWInitial);
			splitColorHistSpecPrevRG = m_RenderGraph.ImportImage(m_RT_SplitColorHistoryImage[1][prev], "RTSplitColorHistSpecPrev", computeRWInitial);
			// Reuse the unconditional handles (importing the same image twice would
			// desync the graph's per-resource state tracking).
			splitMomentsDiffCurrRG = (curr == 0u) ? splitMomentsDiff0RG : splitMomentsDiff1RG;
			splitMomentsDiffPrevRG = (curr == 0u) ? splitMomentsDiff1RG : splitMomentsDiff0RG;
			splitMomentsSpecCurrRG = (curr == 0u) ? splitMomentsSpec0RG : splitMomentsSpec1RG;
			splitMomentsSpecPrevRG = (curr == 0u) ? splitMomentsSpec1RG : splitMomentsSpec0RG;

			splitAtrousDiffPingRG = m_RenderGraph.ImportImage(m_RT_SplitATrousImage[0][0], "RTSplitATrousDiffPing", computeRWInitial);
			splitAtrousDiffPongRG = m_RenderGraph.ImportImage(m_RT_SplitATrousImage[0][1], "RTSplitATrousDiffPong", computeRWInitial);
			splitAtrousSpecPingRG = m_RenderGraph.ImportImage(m_RT_SplitATrousImage[1][0], "RTSplitATrousSpecPing", computeRWInitial);
			splitAtrousSpecPongRG = m_RenderGraph.ImportImage(m_RT_SplitATrousImage[1][1], "RTSplitATrousSpecPong", computeRWInitial);
		}
		else
		{
			accumulationRG = m_RenderGraph.ImportImage(m_RT_AccumulationImage, "RTAccumulation", computeRWInitial);
		}
		// Split a-trous chains (one per signal); iteration i reads chain[i] and
		// writes chain[i + 1]. Iteration 0's output (SplitColorHistory[ch][curr])
		// is what next frame's temporal pass reprojects. Finalize recombines
		// chain[atrousIterations] of both. Only index 0 (SplitTemporalColor) is
		// valid in reference mode.
		const uint32_t atrousIterations = static_cast<uint32_t>(
			glm::clamp(m_AtrousIterations, 2, static_cast<int>(SVGF_ATROUS_MAX_ITERATIONS)));
		const RGTextureHandle splitChainDiffRG[SVGF_ATROUS_MAX_ITERATIONS + 1] = {
			splitTemporalDiffRG, splitColorHistDiffCurrRG, splitAtrousDiffPingRG, splitAtrousDiffPongRG, splitAtrousDiffPingRG, splitAtrousDiffPongRG };
		const RGTextureHandle splitChainSpecRG[SVGF_ATROUS_MAX_ITERATIONS + 1] = {
			splitTemporalSpecRG, splitColorHistSpecCurrRG, splitAtrousSpecPingRG, splitAtrousSpecPongRG, splitAtrousSpecPingRG, splitAtrousSpecPongRG };
		const RGTextureHandle splitAtrousFinalDiffRG = denoiser ? splitChainDiffRG[atrousIterations] : splitTemporalDiffRG;
		const RGTextureHandle splitAtrousFinalSpecRG = denoiser ? splitChainSpecRG[atrousIterations] : splitTemporalSpecRG;
		const RGTextureHandle finalizeSourceRG = denoiser ? splitAtrousFinalSpecRG : accumulationRG;

		const glm::ivec2 viewportResolution = m_ViewportResolution;
		const uint32_t sampleIndex = m_AccumulatedFrames;
		struct SpatialPreResolvePush
		{
			uint32_t Enabled;
		} spatialPreResolvePush{ m_SpatialPreResolve ? 1u : 0u };
		struct TemporalPush
		{
			uint32_t FrameIndex;
			float AntilagStrength;
			float AlphaMin;
			uint32_t SpecVirtualReproj; // 0 = surface reprojection only for specular
		} temporalPush{ m_DenoiserFrameIndex, m_AntilagStrength, m_AlphaMin,
			m_SpecVirtualReproj ? 1u : 0u };
		const float sigmaLuma = m_SigmaLuma;
		const float sigmaDepth = m_SigmaDepth;
		const uint32_t groupsX = (static_cast<uint32_t>(viewportResolution.x) + 15u) / 16u;
		const uint32_t groupsY = (static_cast<uint32_t>(viewportResolution.y) + 15u) / 16u;

		struct FinalizePush
		{
			float Exposure;
			uint32_t DebugView;
			// Denoiser mode filters demodulated illumination, so Finalize must
			// re-multiply by the primary albedo before tone mapping. Not for the
			// debug views (they show G-buffers, not the lit image).
			uint32_t Remodulate;
			uint32_t SplitParity; // m_DenoiserFrameIndex & 1, for the history debug view
		} finalizePush{ m_Exposure, static_cast<uint32_t>(m_DebugView),
			(denoiser && m_DebugView == 0) ? 1u : 0u, curr };

		m_RenderGraph.AddRayTracingPass(
			"PathTrace",
			[radianceRG, normalDepthRG, positionMeshIDRG, albedoRG, motionRG, materialRG,
			 radianceDiffuseRG, radianceSpecularRG, radianceEmissiveRG, specularAlbedoRG, hitDistanceRG,
			 motionSpecularRG, specReflRG, directLumaRG](RenderGraphBuilder& builder)
			{
				builder.ReadWriteStorageImage(radianceRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(albedoRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(motionRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(materialRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(radianceDiffuseRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(radianceSpecularRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(radianceEmissiveRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(specularAlbedoRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(hitDistanceRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(motionSpecularRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(specReflRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
				builder.ReadWriteStorageImage(directLumaRG, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
			},
			[this, viewportResolution](RayTracingCommandList& rayTracing)
			{
				rayTracing.BindRayTracingPipeline(m_RT_Pipeline);
				rayTracing.BindDescriptorSets(m_RT_BindGroup);
				rayTracing.TraceRays(
					*m_ShaderBindingTable,
					(uint32_t)viewportResolution.x,
					(uint32_t)viewportResolution.y,
					1);
			});

		if (denoiser)
		{
			// Spatial pre-resolve: a small fixed edge-aware bilateral over the raw
			// 1-spp diffuse/specular, run before the temporal pass reprojects/
			// accumulates it (MegaLights resolve -> temporal order). Runs before
			// SVGFTemporalSplit so the confidence/variance-sensitive stats later in
			// the chain are not fed raw 1-spp noise.
			m_RenderGraph.AddComputePass(
				"SVGFSpatialPreResolve",
				[radianceDiffuseRG, radianceSpecularRG, normalDepthRG, positionMeshIDRG,
				 radianceDiffuseResolvedRG, radianceSpecularResolvedRG](RenderGraphBuilder& builder)
				{
					builder.ReadWriteStorageImage(radianceDiffuseRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(radianceSpecularRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(radianceDiffuseResolvedRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(radianceSpecularResolvedRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				},
				[this, spatialPreResolvePush, groupsX, groupsY](ComputeCommandList& compute)
				{
					compute.BindPipeline(m_SVGFSpatialPreResolvePipeline);
					compute.BindDescriptorSets(m_SVGFSpatialPreResolveBindGroup);
					compute.SetPushConstants(AsBytes(spatialPreResolvePush));
					compute.Dispatch(groupsX, groupsY, 1);
				});

			// Dual-channel temporal pass: reproject + blend diffuse and specular
			// illumination in one dispatch, write the per-signal EMA to the
			// transient SplitTemporalColor buffers + moments + interim colour
			// history + the shared HistoryGBuffer.
			m_RenderGraph.AddComputePass(
				"SVGFTemporalSplit",
				[radianceDiffuseResolvedRG, radianceSpecularResolvedRG, normalDepthRG, positionMeshIDRG, motionRG,
				 motionSpecularRG, materialRG, specReflRG, specReflPPPrevRG, specReflPPCurrRG,
				 directLumaRG, directLumaPPPrevRG, directLumaPPCurrRG,
				 splitColorHistDiffPrevRG, splitMomentsDiffPrevRG, splitColorHistSpecPrevRG, splitMomentsSpecPrevRG,
				 historyGBufferPrevRG, splitTemporalDiffRG, splitMomentsDiffCurrRG, splitTemporalSpecRG,
				 splitMomentsSpecCurrRG, historyGBufferCurrRG, splitColorHistDiffCurrRG, splitColorHistSpecCurrRG](RenderGraphBuilder& builder)
				{
					builder.ReadWriteStorageImage(radianceDiffuseResolvedRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(radianceSpecularResolvedRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(motionRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(motionSpecularRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(materialRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(specReflRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(specReflPPPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(specReflPPCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(directLumaRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(directLumaPPPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(directLumaPPCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistDiffPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsDiffPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistSpecPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsSpecPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(historyGBufferPrevRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitTemporalDiffRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsDiffCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitTemporalSpecRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsSpecCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(historyGBufferCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistDiffCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistSpecCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				},
				[this, curr, temporalPush, groupsX, groupsY](ComputeCommandList& compute)
				{
					compute.BindPipeline(m_SVGFTemporalSplitPipeline);
					compute.BindDescriptorSets(m_SVGFTemporalSplitBindGroup[curr]);
					compute.SetPushConstants(AsBytes(temporalPush));
					compute.Dispatch(groupsX, groupsY, 1);
				});

			// History-fix: spatial bootstrap for short-history pixels (disocclusion
			// / moving leading edge) before the a-trous. Reads the temporal result
			// from the SplitColorHistory[curr] copy, writes the fixed result back
			// into SplitTemporalColor (a-trous iteration 0's input). Passthrough for
			// converged pixels -> no effect on a still camera.
			m_RenderGraph.AddComputePass(
				"SVGFHistoryFix",
				[splitTemporalDiffRG, splitTemporalSpecRG, splitColorHistDiffCurrRG, splitColorHistSpecCurrRG,
				 splitMomentsDiffCurrRG, splitMomentsSpecCurrRG, normalDepthRG, positionMeshIDRG](RenderGraphBuilder& builder)
				{
					builder.ReadWriteStorageImage(splitTemporalDiffRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitTemporalSpecRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistDiffCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitColorHistSpecCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsDiffCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(splitMomentsSpecCurrRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				},
				[this, curr, groupsX, groupsY](ComputeCommandList& compute)
				{
					compute.BindPipeline(m_SVGFHistoryFixPipeline);
					compute.BindDescriptorSets(m_SVGFHistoryFixBindGroup[curr]);
					compute.Dispatch(groupsX, groupsY, 1);
				});

			// Split a-trous: atrousIterations passes per channel, step size
			// doubling (1,2,4,...). One pipeline, one pre-wired bind group per
			// (parity, channel, iteration); Channel is a push field selecting the
			// specular roughness scaling. Iteration 0 writes
			// SplitColorHistory[ch][curr] (the feedback the temporal pass
			// reprojects next frame) and reads that signal's Moments[curr].z for
			// the thin-history spatial-variance fallback.
			const uint32_t denoiserFrameIndex = m_DenoiserFrameIndex;
			const RGTextureHandle* splitChainRG[2] = { splitChainDiffRG, splitChainSpecRG };
			const RGTextureHandle splitMomentsChannelRG[2] = { splitMomentsDiffCurrRG, splitMomentsSpecCurrRG };

			struct AtrousSplitPush
			{
				int32_t StepSize;
				float SigmaLuma;
				float SigmaDepth;
				uint32_t FirstIteration;
				uint32_t FrameIndex;
				uint32_t Channel; // 0 diffuse, 1 specular
			};

			for (uint32_t channel = 0; channel < 2; ++channel)
			{
				for (uint32_t iteration = 0; iteration < atrousIterations; ++iteration)
				{
					const RGTextureHandle inRG  = splitChainRG[channel][iteration];
					const RGTextureHandle outRG = splitChainRG[channel][iteration + 1];
					const RGTextureHandle momRG = splitMomentsChannelRG[channel];
					const AtrousSplitPush push{ 1 << iteration, sigmaLuma, sigmaDepth,
						iteration == 0u ? 1u : 0u, denoiserFrameIndex, channel };
					const std::string passName = "SVGFAtrousSplit " +
						std::string(channel == 0 ? "D" : "S") + std::to_string(iteration);

					m_RenderGraph.AddComputePass(
						passName.c_str(),
						[inRG, outRG, normalDepthRG, positionMeshIDRG, momRG, materialRG, hitDistanceRG](RenderGraphBuilder& builder)
						{
							builder.ReadWriteStorageImage(inRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(outRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(momRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(materialRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
							builder.ReadWriteStorageImage(hitDistanceRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
						},
						[this, curr, channel, iteration, push, groupsX, groupsY](ComputeCommandList& compute)
						{
							compute.BindPipeline(m_SVGFAtrousSplitPipeline);
							compute.BindDescriptorSets(m_SVGFAtrousSplitBindGroup[curr][channel][iteration]);
							compute.SetPushConstants(AsBytes(push));
							compute.Dispatch(groupsX, groupsY, 1);
						});
				}
			}
		}
		else
		{
			m_RenderGraph.AddComputePass(
				"ReferenceAccumulate",
				[radianceRG, accumulationRG](RenderGraphBuilder& builder)
				{
					builder.ReadWriteStorageImage(radianceRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
					builder.ReadWriteStorageImage(accumulationRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				},
				[this, sampleIndex, groupsX, groupsY](ComputeCommandList& compute)
				{
					compute.BindPipeline(m_ReferenceAccumulatePipeline);
					compute.BindDescriptorSets(m_ReferenceAccumulateBindGroup);
					compute.SetPushConstants(AsBytes(sampleIndex));
					compute.Dispatch(groupsX, groupsY, 1);
				});
		}

		m_RenderGraph.AddComputePass(
			"Finalize",
			[finalizeSourceRG, outputRG, normalDepthRG, positionMeshIDRG, albedoRG, motionRG, materialRG,
			 radianceDiffuseRG, radianceSpecularRG, radianceEmissiveRG, specularAlbedoRG,
			 splitTemporalSpecRG, splitAtrousFinalDiffRG, splitAtrousFinalSpecRG,
			 splitMomentsSpec0RG, splitMomentsSpec1RG, splitMomentsDiff0RG, splitMomentsDiff1RG](RenderGraphBuilder& builder)
			{
				builder.ReadWriteStorageImage(finalizeSourceRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(outputRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(normalDepthRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(positionMeshIDRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(albedoRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(motionRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(materialRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(radianceDiffuseRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(radianceSpecularRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(radianceEmissiveRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(specularAlbedoRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitTemporalSpecRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitAtrousFinalDiffRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitAtrousFinalSpecRG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitMomentsSpec0RG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitMomentsSpec1RG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitMomentsDiff0RG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				builder.ReadWriteStorageImage(splitMomentsDiff1RG, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			},
			[this, finalizePush, groupsX, groupsY](ComputeCommandList& compute)
			{
				compute.BindPipeline(m_FinalizePipeline);
				compute.BindDescriptorSets(m_FinalizeBindGroup);
				compute.SetPushConstants(AsBytes(finalizePush));
				compute.Dispatch(groupsX, groupsY, 1);
			});

		m_RenderGraph.Compile();

		commandBuffer->BeginRenderThread();
		m_RenderGraph.Execute(*commandBuffer, frameIndex, &Renderer::GetFrameStatistics());
		commandBuffer->EndRenderThread();
		commandBuffer->SubmitRenderThread();

		// Keep sampleIndex + 1 representable in the shader.
		if (m_AccumulatedFrames < std::numeric_limits<uint32_t>::max() - 1u)
			++m_AccumulatedFrames;

		// Advances every rendered frame. Reset only on resize, structural scene
		// edits, and denoiser toggle -- NOT on camera motion (reprojection
		// handles that). Zero this frame means "no valid history" to SVGFTemporal.
		// Wrapping is harmless (used only for ping-pong parity + RNG decorrelation).
		++m_DenoiserFrameIndex;
	}

	void PathTracerSceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		if (m_ViewportResolution.x == width && m_ViewportResolution.y == height)
			return;

		m_ViewportResolution.x = width;
		m_ViewportResolution.y = height;

		// History/reprojection buffers are resolution-bound; restart both the
		// reference mean (via CreateRTImages -> ResetAccumulation) and the
		// denoiser frame counter.
		m_DenoiserFrameIndex = 0;

		ReleaseRTImages();
		CreateRTImages();
	}

	bool PathTracerSceneRenderer::RegisterMesh(
		RenderObjectID renderObjectID,
		const Ref<Mesh>& mesh,
		const Ref<MaterialTable>& materialTable,
		const glm::mat4& transform,
		int32_t submeshIndex)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool registered = m_RenderScene->RegisterMesh(renderObjectID, mesh, materialTable, transform, submeshIndex);
		if (registered)
		{
			ResetAccumulation();
			// Structural scene edit: the TLAS is rebuilt and instanceCustomIndex
			// values renumber, so mesh-id history validation would reject most of
			// the buffer anyway -- drop the denoiser history wholesale.
			m_DenoiserFrameIndex = 0;
		}
		return registered;
	}

	void PathTracerSceneRenderer::Render(const SceneRendererCamera& sceneRendererCamera)
	{
		CORE_PROFILE_FUNCTION();

		if (HasCameraChanged(sceneRendererCamera))
		{
			ResetAccumulation();
			m_PreviousView = sceneRendererCamera.ViewMatrix;
			m_PreviousProjection = sceneRendererCamera.Camera.GetProjection();
			m_HasPreviousCamera = true;
		}

		m_SceneData.SceneRendererCamera = sceneRendererCamera;

		// Seed the renderer exposure from the camera once; from then on it is a
		// free control on the Renderer panel (see OnImGuiRender).
		if (!m_ExposureInitialized)
		{
			m_Exposure = sceneRendererCamera.Camera.GetExposure();
			m_ExposureInitialized = true;
		}

		if constexpr (ENABLE_ACCELERATION_STRUCTURES)
		{
			CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
			m_RenderScene->PrepareRayTracing();

			// Nothing left to trace. Do not bind/trace an invalid TLAS.
			if (!m_RenderScene->HasValidTLAS())
				return;

			UpdateRTMaterialBuffer();
			UpdateRTInstanceBuffer();
			UpdateRTTextureTable();
			UpdateRTEnvironment();

			BindGroup* rtBindGroup = ResourceManager::GetBindGroup(m_RT_BindGroup);
			CORE_ASSERT(rtBindGroup, "Invalid RT bind group");

			rtBindGroup->SetAccelerationStructure(
				m_RenderScene->GetTLAS(),
				RT_TLAS_BINDING
			);
		}

		FlushRenderPasses();
	}

	bool PathTracerSceneRenderer::IsRegistered(RenderObjectID renderObjectID)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		return m_RenderScene->IsRegistered(renderObjectID);
	}

	bool PathTracerSceneRenderer::UnregisterMesh(RenderObjectID renderObjectID)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool unregistered = m_RenderScene->UnregisterMesh(renderObjectID);
		if (unregistered)
		{
			ResetAccumulation();
			m_DenoiserFrameIndex = 0; // TLAS rebuild renumbers instances; see RegisterMesh.
		}
		return unregistered;
	}

	void PathTracerSceneRenderer::UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (!m_RenderScene->IsRegistered(renderObjectID))
			return;

		m_RenderScene->UpdateMeshTransform(renderObjectID, transform);
		ResetAccumulation();
	}

	bool PathTracerSceneRenderer::UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const bool updated = m_RenderScene->UpdateMeshMaterials(renderObjectID, materialTable);
		if (updated)
			ResetAccumulation();
		return updated;
	}

	void PathTracerSceneRenderer::OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (m_RenderScene->UpdateMaterialProxy(materialAsset))
			ResetAccumulation();
	}

	void PathTracerSceneRenderer::SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		const SkyLightProxy previousSkyLight = m_RenderScene->GetSkyLight();
		m_RenderScene->SetSkyLight(environment, intensity, sampleLOD);
		const SkyLightProxy& currentSkyLight = m_RenderScene->GetSkyLight();

		constexpr float epsilon = 0.000001f;
		const bool skyLightChanged =
			previousSkyLight.RadianceMap != currentSkyLight.RadianceMap ||
			previousSkyLight.IrradianceMap != currentSkyLight.IrradianceMap ||
			std::abs(previousSkyLight.Intensity - currentSkyLight.Intensity) > epsilon ||
			std::abs(previousSkyLight.SampleLOD - currentSkyLight.SampleLOD) > epsilon;

		if (skyLightChanged)
			ResetAccumulation();
	}

	void PathTracerSceneRenderer::SetDirectionalLights(std::span<const DirectionalLight> directionalLights)
	{
		CORE_ASSERT(m_RenderScene, "RenderScene is not initialized");
		if (m_RenderScene->SetDirectionalLights(directionalLights))
			ResetAccumulation();
	}

	// Hover tooltip for the control immediately preceding the call. Each RT
	// setting carries a one-line explanation of what it does and how to use it.
	static void SettingHelp(const char* text)
	{
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", text);
	}

	void PathTracerSceneRenderer::OnImGuiRender()
	{
		ImGui::Begin("Renderer");
		{
			if (ui::PropertyGridHeader("Path Tracer", true))
			{
				ImGui::Separator();
				// Integrator changes invalidate every accumulated sample and the
				// denoiser history; restart both.
				bool integratorChanged = false;
				integratorChanged |= ImGui::SliderInt("Max bounces", &m_MaxBounces, 1, 16);
				SettingHelp("Maximum path length. Each bounce adds a level of indirect light: "
					"higher = richer global illumination and darker, better-grounded contact "
					"shadows, at a roughly proportional cost. 1 = direct lighting only.");
				integratorChanged |= ImGui::SliderInt("Samples / frame", &m_SamplesPerFrame, 1, 16);
				SettingHelp("Independent primary paths traced per pixel each frame. The single "
					"biggest lever on input noise and edge aliasing: N samples divide both by "
					"about N, at about Nx GPU cost. The denoiser then cleans up what remains.");
				if (integratorChanged)
				{
					ResetAccumulation();
					m_DenoiserFrameIndex = 0;
				}
				ui::EndTreeNode();
			}

			if (ui::PropertyGridHeader("Statistics", true))
			{
				ImGui::Separator();
				ImGui::Text("Accumulated frames: %u  (x%d spp = %llu paths/pixel)",
					m_AccumulatedFrames, m_SamplesPerFrame,
					static_cast<unsigned long long>(m_AccumulatedFrames) * static_cast<unsigned long long>(std::max(m_SamplesPerFrame, 1)));
				if (m_RenderScene)
				{
					ImGui::Text("Mesh instances: %zu", m_RenderScene->GetRasterMeshInstances().size());
					ImGui::Text("Triangles: %u", m_RenderScene->GetTotalTriangleCount());
					const SkyLightProxy& skyLight = m_RenderScene->GetSkyLight();
					ImGui::Text("SkyLight intensity: %.3f", skyLight.Intensity);
					ImGui::Text("SkyLight radiance LOD: %.0f", skyLight.SampleLOD);
					ImGui::Text("Directional lights: %u", m_RenderScene->GetDirectionalLightCount());
					if (m_RenderScene->GetDirectionalLightCount() > 0)
						ImGui::Text("Directional light 0 intensity: %.3f", m_RenderScene->GetDirectionalLights()[0].Intensity);
				}
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				GPUMemoryStats memoryStats = ResourceAllocator::GetStats();
				std::string used = utils::BytesToString(memoryStats.Used);
				std::string free = utils::BytesToString(memoryStats.Free);
				ImGui::Text("Used VRAM: %s", used.c_str());
				ImGui::Text("Free VRAM: %s", free.c_str());
				ui::EndTreeNode();
			}

			if (ui::PropertyGridHeader("Post Process", true))
			{
				ImGui::Separator();
				ImGui::DragFloat("Exposure", &m_Exposure, 0.01f, 0.0f, 32.0f);
				SettingHelp("Linear multiplier applied to the HDR image before tone mapping. "
					"Seeded once from the camera, then a free control here.");

				if (ImGui::Checkbox("Denoiser (SVGF)", &m_DenoiserEnabled))
				{
					// Start each mode from a clean slate: the denoiser from an
					// empty history, the reference accumulator from sample 0 (its
					// buffer is stale after a stretch of denoiser frames).
					m_DenoiserFrameIndex = 0;
					ResetAccumulation();
				}
				SettingHelp("Spatiotemporal Variance-Guided Filtering. Off: unbiased progressive "
					"accumulation, converges only while the view is still. On: real-time denoise "
					"(temporal reprojection + edge-aware spatial filter) that stays usable while "
					"the camera moves.");

				if (m_DenoiserEnabled)
				{
					// Live SVGF tuning. Fewer iterations / smaller sigmas keep more
					// detail at the cost of residual noise; larger = smoother.
					ImGui::SliderInt("A-trous iterations", &m_AtrousIterations, 2, static_cast<int>(SVGF_ATROUS_MAX_ITERATIONS));
					SettingHelp("Spatial filter passes, each doubling its reach (1, 2, 4, ...). "
						"More = smoother wide-area denoise but softer fine detail and more cost. "
						"3-4 is usually the sweet spot.");

					ImGui::SliderFloat("Sigma luma", &m_SigmaLuma, 0.5f, 8.0f);
					SettingHelp("How much brightness difference between neighbours the spatial "
						"filter tolerates. Lower = tighter: keeps shadow edges and detail sharp, "
						"leaves more grain. Higher = smoother: can wash out contact shadows.");

					ImGui::SliderFloat("Sigma depth", &m_SigmaDepth, 0.1f, 4.0f);
					SettingHelp("How sharply the spatial filter stops at depth discontinuities. "
						"Lower = crisper separation between surfaces at different distances; "
						"higher = more light bleed across them.");

					ImGui::SliderFloat("Firefly clamp", &m_FireflyClamp, 0.0f, 64.0f);
					SettingHelp("Caps the peak brightness a single path may contribute (denoiser "
						"mode only). Removes rare bright speckles the filter would otherwise smear "
						"into blobs. Too low dims legitimate highlights; 0 disables.");

					ImGui::SliderFloat("Antilag (A-SVGF)", &m_AntilagStrength, 0.0f, 4.0f);
					SettingHelp("Detects sudden lighting changes (a light toggling, a shadow "
						"sweeping in) per pixel and discards stale history there, so the image "
						"re-adapts in a frame or two instead of ghosting. 1 reacts only to changes "
						"well past the noise band (~4 std-devs); higher reacts sooner but can "
						"misfire on noise and add grain. 0 disables.");

					ImGui::SliderFloat("Alpha min", &m_AlphaMin, 0.0f, 0.3f);
					SettingHelp("Temporal history floor. Lower = the running mean keeps more "
						"frames = much cleaner while the view is still, but more lag on lighting "
						"changes (Antilag mitigates that). 0.2 is the SVGF default; drop to ~0.05 "
						"for a near-static camera.");

					ImGui::Checkbox("Specular virtual reproj", &m_SpecVirtualReproj);
					SettingHelp("Reproject the specular history along the reflected ray (NRD ReLAX "
						"style) validated against the reflection geometry, so a reflection tracks "
						"the surface under camera motion instead of boiling. Off: surface "
						"reprojection only (parallax distrust + spatial-mean blend still on). "
						"Static image is identical either way.");

					ImGui::Checkbox("Spatial pre-resolve", &m_SpatialPreResolve);
					SettingHelp("Small fixed edge-aware bilateral over the raw 1-spp diffuse/specular, "
						"run before the temporal pass accumulates it (MegaLights resolve->temporal "
						"order). Gives the temporal EMA and firefly clamp a less noisy signal to "
						"start from. Off: the temporal pass accumulates the raw 1-spp signal "
						"directly (A/B baseline).");
				}

				// Order MUST match the DebugView switch in Finalize.glsl.
				static const char* kDebugViews[] = {
					"Lit", "Motion vectors", "Normals", "Linear depth", "Albedo", "Mesh ID", "Denoiser variance",
					"Material (rough/metal/tightness)",
					"Split: recombined (raw)",
					"Split: spec alpha/history (R=a G=hist)", "Split: spec EMA raw",
					"Split: diff alpha/history (R=a G=hist)" };
				ImGui::Combo("Debug view", &m_DebugView, kDebugViews, IM_ARRAYSIZE(kDebugViews));
				SettingHelp("Replaces the lit image with a raw denoiser input for inspection: "
					"motion vectors, world normals, linear depth, primary albedo, a per-instance "
					"mesh-id hash, or the denoiser variance (dark = converged; bright everywhere "
					"on a still camera means temporal accumulation is failing).");
				ui::EndTreeNode();
			}

			if (CommandBuffer* commandBuffer = Renderer::GetMainCommandBuffer(m_Specification.SwapChainTarget))
				m_RenderGraph.OnImGuiRender(*commandBuffer);
		}
		ImGui::End();
	}

	Handle<ImageView> PathTracerSceneRenderer::GetFinalPassImage() const
	{
		return m_RT_OutputImageView;
	}
}

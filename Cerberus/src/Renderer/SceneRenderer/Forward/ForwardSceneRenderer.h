#pragma once

#include "Renderer/Descriptor/BindGroup.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RHI/RenderingInfo.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"
#include "Renderer/RHI/Buffers/StorageBufferSet.h"
#include "Renderer/RHI/Buffers/UniformBufferSet.h"
#include "Renderer/Scene/RenderScene.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Renderer/SceneRenderer/Forward/Passes/ForwardPass.h"
#include "Renderer/SceneRenderer/Forward/Passes/SkyPass.h"
#include "Renderer/SceneRenderer/Forward/Passes/PreDepthPass.h"
#include "Renderer/SceneRenderer/Forward/Passes/ShadowPass.h"
#include "Renderer/SceneRenderer/Forward/Passes/CompositePass.h"

namespace cerberus
{
	// Modern raster reference renderer: PreDepth -> Shadow (CSM) -> Sky/Forward
	// -> Composite, ordered and barriered by a RenderGraph rather than each
	// scope hand-writing its own BeginRendering/EndRendering transitions.
	class ForwardSceneRenderer final : public SceneRenderer
	{
	public:
		ForwardSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		~ForwardSceneRenderer() override;

		void SetViewportSize(uint32_t width, uint32_t height) override;

		bool IsRegistered(RenderObjectID renderObjectID) override;
		bool RegisterMesh(RenderObjectID renderObjectID, const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		bool UnregisterMesh(RenderObjectID renderObjectID) override;
		void UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform) override;
		bool UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable) override;
		void OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset) override;
		void SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD) override;
		void SetDirectionalLights(std::span<const DirectionalLight> directionalLights) override;

		void Render(const SceneRendererCamera& sceneRendererCamera) override;
		void OnImGuiRender() override;
		Handle<ImageView> GetFinalPassImage() const override;

	private:
		void CreateRasterResources();
		void DestroyRasterResources();
		void UpdateCameraBuffer();
		void UpdateLightingBuffer();
		void UpdateShadowBuffer();
		void UpdateMaterialBuffer();
		void UpdateTextureTable();
		void UpdateEnvironmentBindings();
		void UpdateCompositeBindings();

	private:
		struct CameraUB
		{
			glm::mat4 ViewProjection = glm::mat4(1.0f);
			glm::mat4 InverseViewProjection = glm::mat4(1.0f);
			glm::vec4 CameraPosition = glm::vec4(0.0f);
			// x = exposure, y/z/w = unused.
			glm::vec4 Presentation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		};
		static_assert(sizeof(CameraUB) == 160, "CameraUB must match the Forward shaders");

		struct DirectionalLightGPU
		{
			glm::vec4 DirectionAndIntensity = glm::vec4(0.0f);
			glm::vec4 Radiance = glm::vec4(0.0f);
		};

		struct ForwardLightingUB
		{
			// x = SkyLight intensity, y = display radiance LOD, z/w = unused.
			glm::vec4 SkyLightParameters = glm::vec4(0.0f);
			// x = active directional-light count, y/z/w = unused.
			glm::uvec4 DirectionalLightParameters = glm::uvec4(0u);
			std::array<DirectionalLightGPU, MaxDirectionalLightCount> DirectionalLights{};
		};
		static_assert(sizeof(ForwardLightingUB) == 160, "ForwardLightingUB must match the Forward shaders");

		struct ShadowUB
		{
			std::array<glm::mat4, ShadowCascadeCount> CascadeViewProjection{};
			// One component per cascade; distance-from-camera thresholds, not
			// view-space Z, so the fragment shader doesn't need a view matrix.
			glm::vec4 CascadeSplitDepths = glm::vec4(0.0f);
		};
		static_assert(sizeof(ShadowUB) == 272, "ShadowUB must match ForwardPBR.frag");

		SceneRendererSpecification m_Specification;
		glm::uvec2 m_ViewportResolution = glm::uvec2(1u);
		Ref<Scene> m_Scene;
		SceneRendererCamera m_SceneRendererCamera{};
		Scope<RenderScene> m_RenderScene;

		// Dynamic Rendering has no framebuffer object; the SceneRenderer owns its
		// render targets directly so passes can reference the same target, e.g.
		// PreDepth and Forward both target SceneDepth.
		Handle<RenderTargetTexture> m_SceneColor;
		Handle<RenderTargetTexture> m_SceneDepth;
		Handle<RenderTargetTexture> m_CompositeOutput;
		std::array<Handle<RenderTargetTexture>, ShadowCascadeCount> m_ShadowMaps;
		std::array<ShadowCascadeData, ShadowCascadeCount> m_ShadowCascades;
		Handle<GraphicsPipeline> m_PreDepthPipeline;
		Handle<GraphicsPipeline> m_ShadowPipeline;
		Handle<GraphicsPipeline> m_SkyPipeline;
		Handle<GraphicsPipeline> m_GeometryPipeline;
		Handle<GraphicsPipeline> m_CompositePipeline;

		Handle<BindGroupLayout> m_PreDepthBindGroupLayout;
		Handle<BindGroup> m_PreDepthBindGroup;
		Handle<BindGroupLayout> m_SkyBindGroupLayout;
		Handle<BindGroup> m_SkyBindGroup;
		Handle<BindGroupLayout> m_GeometryBindGroupLayout;
		Handle<BindGroup> m_GeometryBindGroup;
		Handle<BindGroupLayout> m_CompositeBindGroupLayout;
		Handle<BindGroup> m_CompositeBindGroup;

		Ref<UniformBufferSet> m_CameraBufferSet;
		Ref<UniformBufferSet> m_LightingBufferSet;
		Ref<UniformBufferSet> m_ShadowBufferSet;
		Ref<StorageBufferSet> m_MaterialBufferSet;

		// Pass ordering and barriers for this renderer's frame; rebuilt (Reset()
		// then re-added) every Render() call rather than reconstructed.
		RenderGraph m_RenderGraph;
	};
}

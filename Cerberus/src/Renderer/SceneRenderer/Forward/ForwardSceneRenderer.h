#pragma once

#include "Renderer/Descriptor/BindGroup.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"
#include "Renderer/RHI/Buffers/StorageBufferSet.h"
#include "Renderer/RHI/Buffers/UniformBufferSet.h"
#include "Renderer/Scene/RenderScene.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"

namespace cerberus
{
	// Modern raster reference renderer. It intentionally starts as one forward
	// pass so GraphicsCommandList and RenderScene can be validated before the
	// work is split into reusable passes and, later, scheduled by RenderGraph.
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
		void UpdateMaterialBuffer();
		void UpdateTextureTable();
		void UpdateEnvironmentBindings();

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

		SceneRendererSpecification m_Specification;
		glm::uvec2 m_ViewportResolution = glm::uvec2(1u);
		Ref<Scene> m_Scene;
		SceneRendererCamera m_SceneRendererCamera{};
		Scope<RenderScene> m_RenderScene;

		Handle<RenderPass> m_RenderPass;
		Handle<Framebuffer> m_Framebuffer;
		Handle<GraphicsPipeline> m_SkyPipeline;
		Handle<GraphicsPipeline> m_GeometryPipeline;

		Handle<BindGroupLayout> m_SkyBindGroupLayout;
		Handle<BindGroup> m_SkyBindGroup;
		Handle<BindGroupLayout> m_GeometryBindGroupLayout;
		Handle<BindGroup> m_GeometryBindGroup;

		Ref<UniformBufferSet> m_CameraBufferSet;
		Ref<UniformBufferSet> m_LightingBufferSet;
		Ref<StorageBufferSet> m_MaterialBufferSet;
	};
}

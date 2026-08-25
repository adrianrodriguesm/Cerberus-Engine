#pragma once

#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Renderer/Descriptor/BindGroup.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RHI/Pipeline/Pipeline.h"
#include "Renderer/Scene/RenderScene.h"
#include "Renderer/RayTracing/ShaderBindingTable.h"
#include "Renderer/RHI/Buffers/StorageBufferSet.h"

#include <span>

namespace cerberus
{
	class PathTracerSceneRenderer final : public SceneRenderer
	{
	public:
		PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		virtual ~PathTracerSceneRenderer();

		virtual void SetViewportSize(uint32_t width, uint32_t height) override;

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) override;

		virtual bool RegisterMesh(RenderObjectID renderObjectID, const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		virtual bool UnregisterMesh(RenderObjectID renderObjectID) override;
		virtual void UpdateMeshTransform(RenderObjectID renderObjectID, const glm::mat4& transform) override;
		virtual bool UpdateMeshMaterials(RenderObjectID renderObjectID, const Ref<MaterialTable>& materialTable) override;
		virtual void OnMaterialAssetChanged(const Ref<MaterialAsset>& materialAsset) override;
		virtual void SetSkyLight(const Ref<EnvironmentMap>& environment, float intensity, uint32_t sampleLOD) override;
		virtual void SetDirectionalLights(std::span<const DirectionalLight> directionalLights) override;

		virtual bool IsRegistered(RenderObjectID renderObjectID) override;

		virtual void OnImGuiRender() override;

		virtual Handle<ImageView> GetFinalPassImage() const override;

	private:
		void UpdateUBOData();
		void UpdateRTMaterialBuffer();
		void UpdateRTInstanceBuffer();
		void UpdateRTTextureTable();
		void UpdateRTEnvironment();
		void FlushRenderPasses();
		void ResetAccumulation();
		void CreateRTImages();
		void ReleaseRTImages();
		bool HasCameraChanged(const SceneRendererCamera& sceneRendererCamera) const;

	private:
		struct SceneData
		{
			SceneRendererCamera SceneRendererCamera;
		} m_SceneData;

		struct CameraUB
		{
			glm::mat4 Projection;
			glm::mat4 View;
			glm::mat4 ViewProjection;
			glm::mat4 InverseView;
			glm::mat4 InverseProjection;
			glm::mat4 InverseViewReverseProjection;

			glm::vec4 Position;
			glm::vec4 Forward;
			glm::vec4 Right;
			glm::vec4 Up;

			// x = tanHalfFovY
			// y = aspect
			// z/w = unused
			glm::vec4 Params;
		};

		struct SkyLightUB
		{
			// x = intensity, y = radiance mip LOD, z/w = unused.
			glm::vec4 Parameters = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		};
		static_assert(sizeof(SkyLightUB) == 16, "SkyLightUB must match the std140 shader layout");

		struct PathTracerUB
		{
			// x = samples accumulated before this dispatch
			// y = maximum path bounce count
			// z/w = unused
			glm::uvec4 Parameters = glm::uvec4(0u, 4u, 0u, 0u);

			// x = display exposure
			// y/z/w = unused
			glm::vec4 Presentation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		};
		static_assert(sizeof(PathTracerUB) == 32, "PathTracerUB must match the std140 shader layout");

		struct DirectionalLightGPU
		{
			// xyz = direction from the surface toward the light, w = intensity.
			glm::vec4 DirectionAndIntensity = glm::vec4(0.0f);
			glm::vec4 Radiance = glm::vec4(0.0f);
		};

		struct DirectionalLightUB
		{
			// x = active light count, y/z/w = unused.
			glm::uvec4 Parameters = glm::uvec4(0u);
			std::array<DirectionalLightGPU, MaxDirectionalLightCount> Lights{};
		};
		static_assert(sizeof(DirectionalLightUB) == 144, "DirectionalLightUB must match the std140 shader layout");

	private:
		SceneRendererSpecification m_Specification;

		glm::ivec2 m_ViewportResolution;

		Ref<Scene> m_Scene;

		Handle<ImageView> m_FinalPassImage;

		Ref<UniformBufferSet> m_UniformBufferSetCamera;
		Ref<UniformBufferSet> m_RT_SkyLightBufferSet;
		Ref<UniformBufferSet> m_RT_PathTracerBufferSet;
		Ref<UniformBufferSet> m_RT_DirectionalLightBufferSet;
		Ref<StorageBufferSet> m_RT_MaterialBufferSet;
		Ref<StorageBufferSet> m_RT_InstanceBufferSet;

		// Renderer-side representation of the logical scene.
		Scope<RenderScene> m_RenderScene;

		Handle<BindGroupLayout> m_RT_BindGroupLayout;
		Handle<BindGroup> m_RT_BindGroup;

		Handle<RayTracingPipeline> m_RT_Pipeline;

		Handle<Image> m_RT_OutputImage;
		Handle<ImageView> m_RT_OutputImageView;
		Handle<Image> m_RT_AccumulationImage;
		Handle<ImageView> m_RT_AccumulationImageView;

		uint32_t m_AccumulatedFrames = 0;
		bool m_HasPreviousCamera = false;
		glm::mat4 m_PreviousView = glm::mat4(1.0f);
		glm::mat4 m_PreviousProjection = glm::mat4(1.0f);

		Scope<ShaderBindingTable> m_ShaderBindingTable;

		// Single ray-tracing dispatch pass, ordered/barriered the same way as
		// ForwardSceneRenderer's graph; rebuilt every FlushRenderPasses() call.
		RenderGraph m_RenderGraph;
	};
}

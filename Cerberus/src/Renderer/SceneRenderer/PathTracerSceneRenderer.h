#pragma once

#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Renderer/Resources/BindGroup.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RenderScene.h"
#include "Renderer/ShaderBindingTable.h"
#include "Renderer/StorageBufferSet.h"

#include <span>

namespace cerberus
{
	class TextureAsset;

	class PathTracerSceneRenderer final : public SceneRenderer
	{
	public:
		PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		virtual ~PathTracerSceneRenderer();

		virtual void BeginScene(const SceneRendererCamera& sceneRendererCamera) override;
		virtual void EndScene() override;
		virtual void SubmitMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		virtual void SetViewportSize(uint32_t width, uint32_t height) override;
		virtual void SetSceneContext(const Ref<Scene>& scene) override { m_Scene = scene; }

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) override;

		virtual bool RegisterMesh(const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform) override;
		virtual bool UnregisterMesh(const Ref<Mesh>& mesh) override;
		virtual void UpdateMeshTransform(const Ref<Mesh>& mesh, const glm::mat4& transform) override;

		virtual bool IsRegistered(const Ref<Mesh>& mesh) override;

		virtual void OnImGuiRender() override;

		virtual Handle<ImageView> GetFinalPassImage() const override;

	private:
		void UpdateUBOData();
		void FlushRenderPasses();

	private:
		// CPU-side mirror of the ObjDesc struct used in ray tracing shaders.
		// One entry per mesh instance / submesh instance.
		struct RayTracingObjectDesc
		{
			uint64_t VertexBufferAddress;
			uint64_t IndexBufferAddress;
			uint64_t MaterialBufferAddress;
			uint64_t MaterialIndexBufferAddress;

			uint32_t FirstIndex;
			int32_t  BaseVertex;
			uint32_t TextureArrayOffset;
			uint32_t Reserved;
		};

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

		struct ShaderVertex
		{
			glm::vec3 pos;
			glm::vec3 nrm;
			glm::vec3 color;
			glm::vec2 texCoord;
		};

	private:
		SceneRendererSpecification m_Specification;

		glm::ivec2 m_ViewportResolution;

		Ref<Scene> m_Scene;

		Handle<ImageView> m_FinalPassImage;

		Handle<Framebuffer> m_Framebuffer;
		Handle<RenderPass> m_RenderPass;
		Handle<GraphicsPipeline> m_Pipeline;

		Ref<UniformBufferSet> m_UniformBufferSet;
		Ref<UniformBufferSet> m_UniformBufferSetCamera;
		Ref<StorageBufferSet> m_RayTracingObjectDescSet;

		// Renderer-side representation of the logical scene.
		Scope<RenderScene> m_RenderScene;

		Handle<BindGroupLayout> m_BindGroup0Layout;
		Handle<BindGroup> m_BindGroup0;

		Handle<BindGroupLayout> m_BindGroup1Layout;
		Handle<BindGroup> m_BindGroup1;

		Handle<BindGroupLayout> m_RT_BindGroupLayout;
		Handle<BindGroup> m_RT_BindGroup;

		Handle<RayTracingPipeline> m_RT_Pipeline;

		Handle<Image> m_RT_OutputImage;
		Handle<ImageView> m_RT_OutputImageView;

		Scope<ShaderBindingTable> m_ShaderBindingTable;

		struct PushConstantRay
		{
			glm::vec4 ClearColor;
			glm::vec3 LightPosition;
			float LightIntensity;
			int LightType;
		} m_RT_PushConstant;
	};
}

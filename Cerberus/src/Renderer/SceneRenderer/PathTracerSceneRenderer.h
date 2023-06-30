#pragma once
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include "Scene/Scene.h"
#include "Renderer/AccelerationStructure.h"
#include "Renderer/Backend/MeshProxy.h"
#include "Renderer/Backend/BindGroup.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Framebuffer.h"
#include <span>
namespace cerberus
{
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
		void FlushRenderPasses();
	private:
		SceneRendererSpecification m_Specification;

		glm::ivec2 m_ViewportResolution;

		CommandBuffer* m_CommandBuffer;

		Ref<Scene> m_Scene;
		
		Handle<ImageView> m_FinalPassImage;

		Handle<Framebuffer> m_Framebuffer;
		Handle<RenderPass> m_RenderPass;
		Handle<GraphicsPipeline> m_Pipeline;

		Ref<UniformBufferSet> m_UniformBufferSet;

		// Texture test
		Scope<TextureAsset> m_TextureAsset;
		Handle<Texture2D> m_TextureRonaldo;

		struct MeshRegister
		{
			struct AccelerationStructureInfo
			{
				std::vector<Handle<MeshSourceProxy>> BLASToBuild;
				bool UpdateTLAS = false;
				Handle<AccelerationStructure> TLAS;
			} ASRegisterInfo;

			std::unordered_map<MeshHandle, std::vector<Handle<MeshInstanceProxy>>> MeshInstanceRegistered;
			std::unordered_map<MeshSourceHandle, Handle<MeshSourceProxy>> MeshSourceRegistered;
			std::unordered_map<AssetHandle, Handle<BindGroup>> MaterialRegistered;

		} m_MeshRegister;

		Handle<BindGroupLayout> m_RTBindGroupLayout;
		Handle<BindGroup> m_RTBindGroup;

		VkDescriptorPool                                m_rtDescPool;
		VkDescriptorSetLayout                           m_rtDescSetLayout;
		VkDescriptorSet                                 m_rtDescSet;
		
	};
}
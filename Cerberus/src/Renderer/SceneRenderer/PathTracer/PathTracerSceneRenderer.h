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
	class ComputePipeline;

	class PathTracerSceneRenderer final : public SceneRenderer
	{
	public:
		PathTracerSceneRenderer(Ref<Scene> scene, const SceneRendererSpecification& spec = {});
		virtual ~PathTracerSceneRenderer();

		virtual void SetViewportSize(uint32_t width, uint32_t height) override;

		virtual void Render(const SceneRendererCamera& sceneRendererCamera) override;

		virtual bool RegisterMesh(RenderObjectID renderObjectID, const Ref<Mesh>& mesh, const Ref<MaterialTable>& materialTable, const glm::mat4& transform, int32_t submeshIndex = -1) override;
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
		// Compute pipelines/bind groups for reference-mode accumulation + tone
		// mapping. Created once; their storage-image descriptors are refreshed by
		// CreateRTImages() whenever the RT images are (re)created.
		void CreatePostProcessPipelines();
		void ReleasePostProcessPipelines();
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

			// Previous frame's ViewProjection, for denoiser temporal reprojection.
			glm::mat4 PreviousViewProjection;

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
		uint64_t m_TextureRegistryRevision = 0;

		// Renderer-side representation of the logical scene.
		Scope<RenderScene> m_RenderScene;

		Handle<BindGroupLayout> m_RT_BindGroupLayout;
		Handle<BindGroup> m_RT_BindGroup;

		Handle<RayTracingPipeline> m_RT_Pipeline;

		// Display-ready output the editor samples (Finalize writes it).
		Handle<Image> m_RT_OutputImage;
		Handle<ImageView> m_RT_OutputImageView;
		// Linear-HDR progressive mean for reference mode (ReferenceAccumulate).
		Handle<Image> m_RT_AccumulationImage;
		Handle<ImageView> m_RT_AccumulationImageView;
		// Raw per-frame linear radiance the raygen writes; input to every
		// post-process path (reference mean today, denoiser later).
		Handle<Image> m_RT_RadianceImage;
		Handle<ImageView> m_RT_RadianceImageView;

		// Diffuse/specular split for the denoiser (Q2RTX A-SVGF / NRD style): the
		// raygen separates the primary path into a diffuse and a specular part and
		// demodulates each (by diffuse albedo / by the preintegrated env BRDF), so
		// the filter sees BRDF-free signals. Emissive/sky is kept raw and never
		// filtered. Finalize recombines. RTRadiance keeps the un-split sum.
		Handle<Image> m_RT_RadianceDiffuseImage;
		Handle<ImageView> m_RT_RadianceDiffuseImageView;
		Handle<Image> m_RT_RadianceSpecularImage;
		Handle<ImageView> m_RT_RadianceSpecularImageView;
		Handle<Image> m_RT_RadianceEmissiveImage;
		Handle<ImageView> m_RT_RadianceEmissiveImageView;
		// Clamped envBRDF the raygen divided specular by; Finalize::Recombine
		// re-multiplies by the exact same value.
		Handle<Image> m_RT_GBufferSpecularAlbedoImage;
		Handle<ImageView> m_RT_GBufferSpecularAlbedoImageView;
		// Hit distances for the denoiser's specular a-trous reach (Step 8):
		// .x = camera->first hit, .y = first hit->first specular bounce hit.
		Handle<Image> m_RT_GBufferHitDistanceImage;
		Handle<ImageView> m_RT_GBufferHitDistanceImageView;
		// Spatially pre-resolved diffuse/specular (SVGFSpatialPreResolve.glsl): a
		// small fixed edge-aware bilateral over the raw 1-spp radiance above, run
		// before SVGFTemporalSplit (MegaLights ShadeLightSamplesCS -> resolve ->
		// temporal order). This is what SVGFTemporalSplit actually reprojects/
		// accumulates; the raw RadianceDiffuse/Specular images stay untouched for
		// the "recombined raw" debug view.
		Handle<Image> m_RT_RadianceDiffuseResolvedImage;
		Handle<ImageView> m_RT_RadianceDiffuseResolvedImageView;
		Handle<Image> m_RT_RadianceSpecularResolvedImage;
		Handle<ImageView> m_RT_RadianceSpecularResolvedImageView;

		// Primary-hit G-buffer consumed by the future spatiotemporal denoiser.
		// Same format/resolution/lifetime as the accumulation target.
		Handle<Image> m_RT_GBufferNormalDepthImage;
		Handle<ImageView> m_RT_GBufferNormalDepthImageView;
		Handle<Image> m_RT_GBufferPositionMeshIDImage;
		Handle<ImageView> m_RT_GBufferPositionMeshIDImageView;
		// Primary-hit albedo (RGBA16F) for demodulation, and screen-space motion
		// vector (RG16F) for temporal reprojection. Written by the raygen.
		Handle<Image> m_RT_GBufferAlbedoImage;
		Handle<ImageView> m_RT_GBufferAlbedoImageView;
		Handle<Image> m_RT_MotionVectorImage;
		Handle<ImageView> m_RT_MotionVectorImageView;
		// Virtual (reflected-ray) motion vector for the specular reprojection,
		// roughness-blended with the surface one by the raygen.
		Handle<Image> m_RT_MotionVectorSpecularImage;
		Handle<ImageView> m_RT_MotionVectorSpecularImageView;
		// Reflection G-buffer the virtual reprojection validates against:
		// .x virtual view-Z, .y reflected-ray hit distance. Raygen writes the
		// current frame; the ping-pong pair carries it to the next.
		Handle<Image> m_RT_SpecReflHistoryImage;
		Handle<ImageView> m_RT_SpecReflHistoryImageView;
		Handle<Image> m_RT_SpecReflHistoryPPImage[2];
		Handle<ImageView> m_RT_SpecReflHistoryPPImageView[2];
		// Primary-hit direct-lighting luma for the A-SVGF temporal gradient.
		// Raygen writes the current frame; the ping-pong pair carries it forward.
		Handle<Image> m_RT_DirectLumaImage;
		Handle<ImageView> m_RT_DirectLumaImageView;
		Handle<Image> m_RT_DirectLumaPPImage[2];
		Handle<ImageView> m_RT_DirectLumaPPImageView[2];
		// Primary-hit roughness (.x) and metalness (.y); lets the denoiser filter
		// sharp specular pixels far more gently than diffuse ones.
		Handle<Image> m_RT_GBufferMaterialImage;
		Handle<ImageView> m_RT_GBufferMaterialImageView;

		// CL-B: reference-mode accumulation + tone mapping as compute passes, so
		// the raygen only ever emits one raw sample and both reference and
		// denoiser modes are a choice of which passes FlushRenderPasses() adds.
		Handle<ComputePipeline> m_ReferenceAccumulatePipeline;
		Handle<BindGroupLayout> m_ReferenceAccumulateBindGroupLayout;
		Handle<BindGroup> m_ReferenceAccumulateBindGroup;
		Handle<ComputePipeline> m_FinalizePipeline;
		Handle<BindGroupLayout> m_FinalizeBindGroupLayout;
		Handle<BindGroup> m_FinalizeBindGroup;

		// SVGF denoiser, diffuse/specular split (Q2RTX A-SVGF / NRD style). Per
		// frame, per signal (0 = diffuse, 1 = specular):
		//   SVGFTemporalSplit -> SplitTemporalColor[signal] (reprojected EMA)
		//   SVGFAtrousSplit xN -> iter 0 writes SplitColorHistory[signal][curr]
		//                         (fed back next frame, Schied's feedback);
		//                         iters 1..N ping-pong SplitATrous[signal][0/1]
		//   Finalize -> recombines the two filtered signals + emissive
		// SplitColorHistory / SplitMoments ping-pong by m_DenoiserFrameIndex & 1;
		// HistoryGBuffer (shared: oct normal, linear depth, mesh id) too.
		Handle<Image> m_RT_HistoryGBufferImage[2];
		Handle<ImageView> m_RT_HistoryGBufferImageView[2];

		Handle<Image> m_RT_SplitColorHistoryImage[2][2];      // [signal][frame parity]
		Handle<ImageView> m_RT_SplitColorHistoryImageView[2][2];
		Handle<Image> m_RT_SplitMomentsImage[2][2];
		Handle<ImageView> m_RT_SplitMomentsImageView[2][2];
		Handle<Image> m_RT_SplitTemporalColorImage[2];        // [signal], per-frame transient
		Handle<ImageView> m_RT_SplitTemporalColorImageView[2];
		Handle<Image> m_RT_SplitATrousImage[2][2];            // [signal][ping/pong]
		Handle<ImageView> m_RT_SplitATrousImageView[2][2];

		// Spatial pre-resolve: runs once, before SVGFTemporalSplit. Single bind
		// group -- unlike the passes below it touches no ping-ponged history, just
		// this frame's raw radiance + G-buffer.
		Handle<ComputePipeline> m_SVGFSpatialPreResolvePipeline;
		Handle<BindGroupLayout> m_SVGFSpatialPreResolveBindGroupLayout;
		Handle<BindGroup> m_SVGFSpatialPreResolveBindGroup;
		// Dual-channel temporal pass: diffuse + specular in one dispatch. Pre-wired
		// per frame parity.
		Handle<ComputePipeline> m_SVGFTemporalSplitPipeline;
		Handle<BindGroupLayout> m_SVGFTemporalSplitBindGroupLayout;
		Handle<BindGroup> m_SVGFTemporalSplitBindGroup[2];
		// History-fix pass (ReLAX HistoryFix): spatial bootstrap for short-history
		// pixels between the temporal pass and the a-trous. Pre-wired per parity.
		Handle<ComputePipeline> m_SVGFHistoryFixPipeline;
		Handle<BindGroupLayout> m_SVGFHistoryFixBindGroupLayout;
		Handle<BindGroup> m_SVGFHistoryFixBindGroup[2];
		// Split a-trous: the same filter run once per channel.
		// [frame parity][signal 0=diffuse 1=specular][iteration]. Pre-wired.
		Handle<ComputePipeline> m_SVGFAtrousSplitPipeline;
		Handle<BindGroupLayout> m_SVGFAtrousSplitBindGroupLayout;
		Handle<BindGroup> m_SVGFAtrousSplitBindGroup[2][2][5];

		bool m_DenoiserEnabled = false;
		// Live SVGF tuning (Post Process panel). Iterations 2..5; smaller sigmas
		// = tighter edge stops (more detail, more residual noise).
		int   m_AtrousIterations = 4;
		// Luminance edge-stop tolerance. Kept low: the Step-7 mean-variance guide
		// already stops the filter on converged edges, so a high value here only
		// melts reflection / contact detail. Raise via the slider if noisy.
		float m_SigmaLuma = 1.5f;
		float m_SigmaDepth = 1.0f;
		// Peak luminance a per-frame sample may carry into the denoiser; tames
		// fireflies SVGF would otherwise smear. <= 0 disables. Denoiser mode only.
		// Deliberately loose by default so it only catches true outliers.
		float m_FireflyClamp = 16.0f;
		// A-SVGF antilag sensitivity (SVGFTemporalSplit.glsl). Higher = reacts to
		// smaller lighting changes; 0 disables. ~1 means "delta must exceed one
		// noise std-dev to count as a real change".
		float m_AntilagStrength = 1.0f;
		// Temporal blend-factor floor. Lower = longer effective history =
		// cleaner when still, more lag when moving. SVGF paper default 0.2.
		// Temporal blend-factor floor, both channels. ~0.05 keeps the loop
		// self-damping (averages out the a-trous feedback and residual per-frame
		// variation) while still converging close to reference on a still camera.
		// Temporal blend FLOOR. 0 = pure progressive mean: on a still camera the
		// EMA converges to reference quality (the A-SVGF gradient still lets it
		// respond instantly to a real lighting change), so the a-trous only has to
		// denoise the transient and Sigma luma can stay low (sharp, no bleed).
		// Raise only if the still image is unstable.
		float m_AlphaMin = 0.0f;
		// Specular history reprojection along the virtual reflection point
		// (NRD ReLAX style) vs. surface motion only. Off = A/B baseline.
		bool  m_SpecVirtualReproj = true;
		// Spatial pre-resolve before the temporal pass (MegaLights resolve->temporal
		// order). Off = SVGFSpatialPreResolve becomes a passthrough, so
		// SVGFTemporalSplit accumulates the raw 1-spp signal directly (A/B baseline).
		bool  m_SpatialPreResolve = true;
		// Finalize's HDR source depends on mode and, in denoiser mode, on the
		// a-trous iteration count (it reads the buffer the last iteration wrote);
		// re-point it only when one of those actually changes.
		bool m_LastFrameDenoiserEnabled = false;
		int  m_FinalizeSourceIterations = -1;

		uint32_t m_AccumulatedFrames = 0;
		bool m_HasPreviousCamera = false;
		glm::mat4 m_PreviousView = glm::mat4(1.0f);
		glm::mat4 m_PreviousProjection = glm::mat4(1.0f);

		// Distinct from m_PreviousView/Projection (which only track the last
		// accepted camera for accumulation resets): this is the ViewProjection
		// of the last actually rendered frame, uploaded for motion vectors.
		glm::mat4 m_PreviousViewProjection = glm::mat4(1.0f);
		bool m_HasPreviousViewProjection = false;

		// Monotonic frame counter for denoiser-mode RNG decorrelation. Unlike
		// m_AccumulatedFrames it does not reset on camera/scene edits -- only on
		// viewport resize -- so consecutive frames keep decorrelating once the
		// raygen stops folding the reference sample index into its seed (CL 5).
		uint32_t m_DenoiserFrameIndex = 0;

		// Tone-map exposure for the Finalize pass. Seeded from the camera on the
		// first frame, then a free renderer control (see OnImGuiRender).
		float m_Exposure = 1.0f;
		bool m_ExposureInitialized = false;

		// Path integrator controls (Path Tracer panel). Changing either restarts
		// both the reference mean and the denoiser history.
		int m_MaxBounces = 4;
		int m_SamplesPerFrame = 1;

		// Finalize DebugView selector: 0 lit, 1 motion, 2 normal, 3 depth,
		// 4 albedo, 5 mesh id.
		int m_DebugView = 0;

		Scope<ShaderBindingTable> m_ShaderBindingTable;

		// Single ray-tracing dispatch pass, ordered/barriered the same way as
		// ForwardSceneRenderer's graph; rebuilt every FlushRenderPasses() call.
		RenderGraph m_RenderGraph;
	};
}

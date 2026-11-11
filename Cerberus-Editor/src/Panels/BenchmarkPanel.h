#pragma once
#include "EditorPanel.h"
#include "Core/FlyingCamera.h"
#include "Core/Timestep.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"

#include <filesystem>
#include <unordered_map>

namespace cerberus
{
	// One saved camera pose for repeatable benchmark captures: position,
	// orientation, FOV, near/far. Stored in a sidecar file next to the scene
	// (<scene>.benchmark.yaml), not inside the .cscene format, so the scene
	// serializer stays untouched -- see docs/tasks/T2-benchmark-capture.md.
	struct BenchmarkCamera
	{
		std::string Name;
		glm::vec3 Position{ 0.0f };
		float Yaw = -90.0f;
		float Pitch = 0.0f;
		float FOV = 55.0f;
		float NearClip = 0.1f;
		float FarClip = 10000.0f;
	};

	// Owns benchmark state end-to-end: the saved-camera list (and its sidecar
	// file) for the current scene, a capture run's state machine, and CSV
	// output. Explicitly an editor-level object -- not a SceneRenderer
	// responsibility and not a render pass (see the task file's "Design
	// constraints"). It only ever reads timing data through
	// SceneRenderer::GetLastFramePassTimings(), never by parsing ImGui state.
	//
	// Sampling has to happen inside EditorLayer's per-frame update/render
	// loop (to set the camera before Render() and read timings back after),
	// which the generic EditorPanel interface has no hook for -- that is
	// what BeginFrame()/EndFrame() are for. Everything else (the camera list,
	// Save/Delete/Go to, the Run button and progress) goes through the usual
	// EditorPanel::OnImGuiRender().
	class BenchmarkPanel final : public EditorPanel
	{
	public:
		void Shutdown() override {}
		void OnImGuiRender(bool& isOpen) override;
		void SetSceneContext(const Ref<Scene>& context) override;

		bool IsRunning() const { return m_RunState != RunState::Idle; }
		// So EditorLayer can letterbox the viewport image to this aspect
		// ratio while running, instead of stretching it to the panel's own
		// (generally different) shape -- see UI_Viewport()'s comment.
		glm::uvec2 GetResolution() const { return m_Resolution; }

		// EditorLayer passes its current SceneRenderer here whenever it
		// (re)creates one, so the Run button -- which fires from inside
		// OnImGuiRender(), not from EditorLayer's own update -- has something
		// to force the resolution/viewport on at run start.
		void SetActiveSceneRenderer(SceneRenderer* sceneRenderer) { m_ActiveSceneRenderer = sceneRenderer; }

		// The editor's own interactive FlyingCamera -- a stable member on
		// EditorLayer, so this only needs setting once (OnAttach), unlike
		// SetActiveSceneRenderer above. Backs "Save current camera" (reads
		// its live pose/FOV/near/far) and "Go to" (SetPose only, the same
		// restore EditorLayer::ApplyEditorCameraFromScene() already does --
		// FOV/near/far stay the live viewport's own).
		void SetLiveCamera(FlyingCamera* camera) { m_LiveCamera = camera; }

		// Called once per frame from EditorLayer::OnUpdate, before the camera
		// used for this frame is decided. Returns the camera to render with
		// this frame while a run is active (the current benchmark camera), or
		// nullptr when idle, in which case the caller keeps using its own.
		const FlyingCamera* BeginFrame();

		// Called from EditorLayer::OnUpdate right after the frame BeginFrame()
		// returned non-null for has been rendered (i.e. after
		// Scene::OnUpdate()), so sceneRenderer's queries for this frame are
		// already recorded. Reads them back, accumulates, and advances the
		// run's state machine (next camera, or finishes the run and writes
		// the CSV).
		void EndFrame(SceneRenderer& sceneRenderer, Timestep ts);

	private:
		enum class RunState { Idle, WarmingUp, Sampling };

		struct PassAccumulator
		{
			RGPassType Type = RGPassType::Graphics;
			std::vector<float> GpuTimesMs;
		};

		struct CSVRow
		{
			std::string Camera;
			std::string Pass;
			std::string Type;
			float MeanMs = 0.0f, MedianMs = 0.0f, MinMs = 0.0f, MaxMs = 0.0f, P95Ms = 0.0f;
			uint32_t Samples = 0;
		};

		std::filesystem::path GetSidecarPath() const;
		void LoadCameras();
		void SaveCameras() const;

		void StartRun();
		void ApplyCurrentCamera();
		void FlushCurrentCameraStats();
		void FinishRun();
		void WriteCSV();

	private:
		Ref<Scene> m_Scene;
		std::vector<BenchmarkCamera> m_Cameras;
		int m_SelectedCameraIndex = -1;

		SceneRenderer* m_ActiveSceneRenderer = nullptr; // non-owning, refreshed by EditorLayer
		FlyingCamera* m_LiveCamera = nullptr; // non-owning, see SetLiveCamera

		RunState m_RunState = RunState::Idle;
		uint32_t m_CurrentCameraIndex = 0;
		uint32_t m_FrameCounter = 0; // frames elapsed since the current camera was applied
		bool m_FrameCountsForSampling = false; // set by BeginFrame, read by EndFrame
		uint32_t m_WarmupFrames = 60;
		uint32_t m_SampleFrames = 300;
		glm::uvec2 m_Resolution = { 1920u, 1080u };

		FlyingCamera m_RunCamera; // built from the active BenchmarkCamera while running

		bool m_PreRunProfilerEnabled = false;
		SceneRendererType m_RunRendererType = SceneRendererType::Forward;
		// Only meaningful when m_RunRendererType is PathTracer -- restored in
		// FinishRun(). See StartRun()'s comment on forcing denoised mode.
		bool m_PreRunDenoiserEnabled = false;

		std::unordered_map<std::string, PassAccumulator> m_PassAccumulators; // current camera only
		std::vector<float> m_FrameTotalMs; // current camera only
		std::vector<float> m_CpuFrameMs; // current camera only

		std::vector<CSVRow> m_PendingRows; // across every camera this run, written out at the end
		std::filesystem::path m_LastWrittenCSVPath;
	};
}

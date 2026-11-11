#include "pch.h"
#include "BenchmarkPanel.h"

#include "Asset/AssetManager.h"
#include "Core/Application.h"
#include "Core/Profiler.h"
#include "ImGui/ImGuiUI.h"
#include "Renderer/Core/RendererContext.h"
#include "Renderer/RHI/Device.h"
#include "Renderer/RHI/Swapchain.h"
#include "Renderer/SceneRenderer/PathTracer/PathTracerSceneRenderer.h"
#include "Renderer/SceneRenderer/SceneRendererType.h"
#include "Utils/FileSystem.h"
#include "Utils/SerializationUtils.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <imgui/imgui.h>
#include <yaml-cpp/yaml.h>

namespace cerberus
{
	namespace
	{
		const char* PresentModeToString(VkPresentModeKHR mode)
		{
			switch (mode)
			{
			case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
			case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
			case VK_PRESENT_MODE_FIFO_KHR: return "Fifo";
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FifoRelaxed";
			default: return "Unknown";
			}
		}

		const char* PassTypeToString(RGPassType type)
		{
			switch (type)
			{
			case RGPassType::Graphics: return "Graphics";
			case RGPassType::Compute: return "Compute";
			case RGPassType::RayTracing: return "RayTracing";
			default: return "Unknown";
			}
		}

		// Sorts a copy of samples to derive mean/median/min/max/p95; empty input
		// yields all zeros rather than asserting, since a pass that was culled
		// for part of a run (e.g. no shadow-casting light from some camera) is
		// expected to have no samples for that camera.
		void ComputeStats(std::vector<float> samples, float& mean, float& median, float& minMs, float& maxMs, float& p95)
		{
			if (samples.empty())
			{
				mean = median = minMs = maxMs = p95 = 0.0f;
				return;
			}

			std::sort(samples.begin(), samples.end());
			minMs = samples.front();
			maxMs = samples.back();

			double sum = 0.0;
			for (float value : samples)
				sum += value;
			mean = static_cast<float>(sum / static_cast<double>(samples.size()));

			const size_t mid = samples.size() / 2;
			median = (samples.size() % 2 == 0)
				? (samples[mid - 1] + samples[mid]) * 0.5f
				: samples[mid];

			const size_t p95Index = std::min(samples.size() - 1,
				static_cast<size_t>(std::ceil(0.95 * static_cast<double>(samples.size()))) - 1);
			p95 = samples[p95Index];
		}

		// Walks up from the working directory looking for the repo root (the
		// folder containing .git), the same way FileSystem locates the engine
		// and project roots -- cheap (a handful of exists() checks), and only
		// called once per run, not per frame.
		std::filesystem::path FindRepoRoot()
		{
			std::filesystem::path search = FileSystem::GetProjectRoot().parent_path();
			for (uint32_t i = 0; i < 8; ++i)
			{
				if (std::filesystem::exists(search / ".git"))
					return search;
				if (!search.has_parent_path() || search.parent_path() == search)
					break;
				search = search.parent_path();
			}
			return FileSystem::GetProjectRoot().parent_path();
		}

		std::string ReadGitCommitHash(const std::filesystem::path& repoRoot)
		{
			std::ifstream headFile(repoRoot / ".git" / "HEAD");
			if (!headFile)
				return "unknown";

			std::string headContents;
			std::getline(headFile, headContents);

			const std::string refPrefix = "ref: ";
			if (headContents.rfind(refPrefix, 0) != 0)
				return headContents.empty() ? "unknown" : headContents; // detached HEAD: already a hash

			std::ifstream refFile(repoRoot / ".git" / headContents.substr(refPrefix.size()));
			if (!refFile)
				return "unknown";

			std::string hash;
			std::getline(refFile, hash);
			return hash.empty() ? "unknown" : hash;
		}
	}

	std::filesystem::path BenchmarkPanel::GetSidecarPath() const
	{
		if (!m_Scene)
			return {};

		const AssetMetadata& metadata = AssetManager::GetMetadata(m_Scene);
		const std::filesystem::path scenePath = FileSystem::ResolvePath(metadata.FilePath.string());
		std::filesystem::path sidecar = scenePath;
		sidecar.replace_extension(".benchmark.yaml");
		return sidecar;
	}

	void BenchmarkPanel::LoadCameras()
	{
		m_Cameras.clear();
		m_SelectedCameraIndex = -1;

		const std::filesystem::path sidecar = GetSidecarPath();
		if (sidecar.empty() || !std::filesystem::exists(sidecar))
			return;

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(sidecar.string());
		}
		catch (const YAML::Exception& exception)
		{
			NV_CORE_ERROR("[BenchmarkPanel] Failed to parse {0}: {1}", sidecar.string(), exception.what());
			return;
		}

		const YAML::Node cameras = root["Cameras"];
		if (!cameras || !cameras.IsSequence())
			return;

		for (const YAML::Node& node : cameras)
		{
			BenchmarkCamera camera;
			camera.Name = node["Name"].as<std::string>("Camera");
			camera.Position = node["Position"].as<glm::vec3>(glm::vec3(0.0f));
			camera.Yaw = node["Yaw"].as<float>(-90.0f);
			camera.Pitch = node["Pitch"].as<float>(0.0f);
			camera.FOV = node["FOV"].as<float>(55.0f);
			camera.NearClip = node["Near"].as<float>(0.1f);
			camera.FarClip = node["Far"].as<float>(10000.0f);
			m_Cameras.push_back(camera);
		}
	}

	void BenchmarkPanel::SaveCameras() const
	{
		const std::filesystem::path sidecar = GetSidecarPath();
		if (sidecar.empty())
			return;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Cameras" << YAML::Value << YAML::BeginSeq;
		for (const BenchmarkCamera& camera : m_Cameras)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << camera.Name;
			out << YAML::Key << "Position" << YAML::Value << camera.Position;
			out << YAML::Key << "Yaw" << YAML::Value << camera.Yaw;
			out << YAML::Key << "Pitch" << YAML::Value << camera.Pitch;
			out << YAML::Key << "FOV" << YAML::Value << camera.FOV;
			out << YAML::Key << "Near" << YAML::Value << camera.NearClip;
			out << YAML::Key << "Far" << YAML::Value << camera.FarClip;
			out << YAML::EndMap;
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(sidecar);
		fout << out.c_str();
	}

	void BenchmarkPanel::SetSceneContext(const Ref<Scene>& context)
	{
		m_Scene = context;
		LoadCameras();
	}

	void BenchmarkPanel::StartRun()
	{
		if (!m_Scene || !m_ActiveSceneRenderer || m_Cameras.empty() || IsRunning())
			return;

		m_RunRendererType = m_Scene->GetRendererType();

		// Force the fixed benchmark resolution now; EditorLayer's per-frame
		// viewport auto-size is gated off by IsRunning() for the duration, so
		// nothing overwrites this until the run ends and the gate reopens.
		m_ActiveSceneRenderer->SetViewportSize(m_Resolution.x, m_Resolution.y);

		m_PreRunProfilerEnabled = Profiler::IsEnabled();
		Profiler::SetEnabled(true);

		// Reference accumulation only converges on a still camera; a benchmark
		// run holds the camera still for its whole warm-up+sample window, so
		// without this a PathTracer run would silently measure an
		// ever-cheaper, not-yet-converged image rather than steady-state
		// denoised cost.
		if (m_RunRendererType == SceneRendererType::PathTracer)
		{
			if (auto* pathTracer = dynamic_cast<PathTracerSceneRenderer*>(m_ActiveSceneRenderer))
			{
				m_PreRunDenoiserEnabled = pathTracer->IsDenoiserEnabled();
				pathTracer->SetDenoiserEnabled(true);
			}
		}

		Swapchain::SetUncappedPresentModeOverride(true);
		if (const Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain())
			swapchain->RecreateSwapchain();

		m_PendingRows.clear();
		m_CurrentCameraIndex = 0;
		ApplyCurrentCamera();
		m_RunState = RunState::WarmingUp;
		m_FrameCounter = 0;
	}

	void BenchmarkPanel::ApplyCurrentCamera()
	{
		const BenchmarkCamera& saved = m_Cameras[m_CurrentCameraIndex];
		const float aspect = static_cast<float>(m_Resolution.x) / static_cast<float>(m_Resolution.y);
		m_RunCamera = FlyingCamera(saved.FOV, aspect, saved.NearClip, saved.FarClip);
		m_RunCamera.SetPose(saved.Position, saved.Yaw, saved.Pitch);

		m_PassAccumulators.clear();
		m_FrameTotalMs.clear();
		m_CpuFrameMs.clear();
	}

	const FlyingCamera* BenchmarkPanel::BeginFrame()
	{
		if (m_RunState == RunState::Idle)
			return nullptr;

		m_FrameCountsForSampling = false;
		if (m_RunState == RunState::WarmingUp)
		{
			++m_FrameCounter;
			if (m_FrameCounter >= m_WarmupFrames)
			{
				m_RunState = RunState::Sampling;
				m_FrameCounter = 0;
			}
		}
		else
		{
			m_FrameCountsForSampling = true;
		}

		return &m_RunCamera;
	}

	void BenchmarkPanel::EndFrame(SceneRenderer& sceneRenderer, Timestep ts)
	{
		if (!m_FrameCountsForSampling)
			return;

		// FrameTotal is derived as the sum of this frame's named passes rather
		// than a second, separate CommandBuffer query -- it is exactly the
		// total the per-pass breakdown below already accounts for, and keeps
		// this panel's only timing dependency GetLastFramePassTimings(). One
		// call, reused for both, since a second call would clear and rebuild
		// the graph's shared timing cache for no benefit.
		float frameTotalMs = 0.0f;
		for (const RGPassTiming& timing : sceneRenderer.GetLastFramePassTimings())
		{
			// Reserve on first sight of a pass name only -- not a per-frame
			// allocation; every later frame for this camera just push_backs
			// into an already-sized vector.
			const std::string passName(timing.Name);
			auto [it, inserted] = m_PassAccumulators.try_emplace(passName);
			if (inserted)
			{
				it->second.Type = timing.Type;
				it->second.GpuTimesMs.reserve(m_SampleFrames);
			}
			it->second.GpuTimesMs.push_back(timing.GPUTimeMs);
			frameTotalMs += timing.GPUTimeMs;
		}

		if (m_FrameTotalMs.capacity() == 0)
			m_FrameTotalMs.reserve(m_SampleFrames);
		m_FrameTotalMs.push_back(frameTotalMs);

		if (m_CpuFrameMs.capacity() == 0)
			m_CpuFrameMs.reserve(m_SampleFrames);
		m_CpuFrameMs.push_back(ts.GetSeconds() * 1000.0f);

		++m_FrameCounter;
		if (m_FrameCounter < m_SampleFrames)
			return;

		FlushCurrentCameraStats();
		++m_CurrentCameraIndex;
		if (m_CurrentCameraIndex >= m_Cameras.size())
		{
			FinishRun();
			return;
		}

		ApplyCurrentCamera();
		m_RunState = RunState::WarmingUp;
		m_FrameCounter = 0;
	}

	void BenchmarkPanel::FlushCurrentCameraStats()
	{
		const std::string& cameraName = m_Cameras[m_CurrentCameraIndex].Name;

		auto appendRow = [&](const std::string& passName, const char* typeName, std::vector<float> samples)
		{
			CSVRow row;
			row.Camera = cameraName;
			row.Pass = passName;
			row.Type = typeName;
			row.Samples = static_cast<uint32_t>(samples.size());
			ComputeStats(std::move(samples), row.MeanMs, row.MedianMs, row.MinMs, row.MaxMs, row.P95Ms);
			m_PendingRows.push_back(std::move(row));
		};

		for (auto& [passName, accumulator] : m_PassAccumulators)
			appendRow(passName, PassTypeToString(accumulator.Type), accumulator.GpuTimesMs);

		appendRow("FrameTotal", "Frame", m_FrameTotalMs);
		appendRow("CPUFrameTime", "CPU", m_CpuFrameMs);
	}

	void BenchmarkPanel::FinishRun()
	{
		Profiler::SetEnabled(m_PreRunProfilerEnabled);

		if (m_RunRendererType == SceneRendererType::PathTracer)
		{
			if (auto* pathTracer = dynamic_cast<PathTracerSceneRenderer*>(m_ActiveSceneRenderer))
				pathTracer->SetDenoiserEnabled(m_PreRunDenoiserEnabled);
		}

		Swapchain::SetUncappedPresentModeOverride(false);
		if (const Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain())
			swapchain->RecreateSwapchain();

		WriteCSV();

		m_RunState = RunState::Idle;
		m_FrameCounter = 0;
	}

	void BenchmarkPanel::WriteCSV()
	{
		const std::filesystem::path repoRoot = FindRepoRoot();
		const std::filesystem::path benchmarksDir = repoRoot / "Benchmarks";
		FileSystem::CreateDirectory(benchmarksDir);

		const std::time_t now = std::time(nullptr);
		std::tm localTime{};
		localtime_s(&localTime, &now);
		char timestamp[32];
		std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H%M%S", &localTime);

		const std::string sceneName = m_Scene
			? AssetManager::GetMetadata(m_Scene).FilePath.stem().string()
			: "UnknownScene";
		const std::string rendererName = std::string(SceneRendererTypeToString(m_RunRendererType));

		const std::filesystem::path csvPath = benchmarksDir /
			(std::string(timestamp) + "_" + sceneName + "_" + rendererName + ".csv");

		std::ofstream out(csvPath, std::ios::out | std::ios::trunc);
		if (!out)
		{
			NV_CORE_ERROR("[BenchmarkPanel] Failed to open {0} for writing", csvPath.string());
			return;
		}

		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(RendererContext::GetCurrentDevice()->GetVulkanPhysicalDevice(), &deviceProperties);

		const Ref<Swapchain> swapchain = Application::Get().GetWindow()->GetSwapchain();
		const VkPresentModeKHR presentMode = swapchain ? swapchain->GetPresentMode() : VK_PRESENT_MODE_FIFO_KHR;

		uint32_t totalTriangleCount = 0, meshInstanceCount = 0;
		if (m_ActiveSceneRenderer)
		{
			totalTriangleCount = m_ActiveSceneRenderer->GetTotalTriangleCount();
			meshInstanceCount = m_ActiveSceneRenderer->GetMeshInstanceCount();
		}

		out << "# scene: " << sceneName << "\n";
		out << "# renderer: " << rendererName << "\n";
		if (m_RunRendererType == SceneRendererType::PathTracer)
			out << "# path_tracer_mode: denoised\n";
		out << "# resolution: " << m_Resolution.x << "x" << m_Resolution.y << "\n";
		out << "# total_triangle_count: " << totalTriangleCount << "\n";
		out << "# mesh_instance_count: " << meshInstanceCount << "\n";
		out << "# gpu: " << deviceProperties.deviceName << "\n";
		out << "# vulkan_api_version: "
			<< VK_API_VERSION_MAJOR(deviceProperties.apiVersion) << "."
			<< VK_API_VERSION_MINOR(deviceProperties.apiVersion) << "."
			<< VK_API_VERSION_PATCH(deviceProperties.apiVersion) << "\n";
		out << "# gpu_driver_version_raw: " << deviceProperties.driverVersion << "\n";
		out << "# present_mode: " << PresentModeToString(presentMode) << "\n";
#if defined(NV_DEBUG)
		out << "# build_configuration: Debug\n";
#else
		out << "# build_configuration: Release\n";
#endif
		out << "# git_commit: " << ReadGitCommitHash(repoRoot) << "\n";
		out << "# warmup_frames: " << m_WarmupFrames << "\n";
		out << "# sample_frames: " << m_SampleFrames << "\n";

		out << "camera,pass,type,mean_ms,median_ms,min_ms,max_ms,p95_ms,samples\n";
		for (const CSVRow& row : m_PendingRows)
		{
			out << row.Camera << "," << row.Pass << "," << row.Type << ","
				<< row.MeanMs << "," << row.MedianMs << "," << row.MinMs << ","
				<< row.MaxMs << "," << row.P95Ms << "," << row.Samples << "\n";
		}

		NV_CORE_INFO("[BenchmarkPanel] Wrote {0}", csvPath.string());
		m_LastWrittenCSVPath = csvPath;
	}

	void BenchmarkPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Benchmark", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (!m_Scene)
		{
			ImGui::TextUnformatted("No scene loaded.");
			ImGui::End();
			return;
		}

		if (IsRunning())
		{
			const char* stateName = m_RunState == RunState::WarmingUp ? "Warming up" : "Sampling";
			const uint32_t total = m_RunState == RunState::WarmingUp ? m_WarmupFrames : m_SampleFrames;
			ImGui::Text("Running: camera %u/%zu (%s %u/%u)",
				m_CurrentCameraIndex + 1, m_Cameras.size(), stateName, m_FrameCounter, total);
			ImGui::TextDisabled("Resolution forced to %ux%u; viewport resumes when the run ends.",
				m_Resolution.x, m_Resolution.y);
			ImGui::End();
			return;
		}

		if (!m_LastWrittenCSVPath.empty())
			ImGui::TextWrapped("Last run: %s", m_LastWrittenCSVPath.string().c_str());

		ImGui::Separator();
		ImGui::TextUnformatted("Benchmark cameras");

		for (int i = 0; i < static_cast<int>(m_Cameras.size()); ++i)
		{
			ImGui::PushID(i);
			const bool selected = (m_SelectedCameraIndex == i);
			if (ImGui::Selectable(m_Cameras[i].Name.c_str(), selected))
				m_SelectedCameraIndex = i;
			// Selectable defaults to filling the rest of the row, which would
			// otherwise sit on top of (and swallow clicks meant for) the Go
			// to/Delete buttons placed on the same line below -- this imgui
			// version (1.84) has no AllowOverlap Selectable flag, so the
			// explicit call is required instead.
			ImGui::SetItemAllowOverlap();

			ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
			if (ImGui::SmallButton("Go to") && m_LiveCamera)
				m_LiveCamera->SetPose(m_Cameras[i].Position, m_Cameras[i].Yaw, m_Cameras[i].Pitch);
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete"))
			{
				m_Cameras.erase(m_Cameras.begin() + i);
				if (m_SelectedCameraIndex == i)
					m_SelectedCameraIndex = -1;
				SaveCameras();
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}

		if (ImGui::Button("Save current camera as benchmark camera") && m_LiveCamera)
		{
			BenchmarkCamera camera;
			camera.Name = "Camera " + std::to_string(m_Cameras.size() + 1);
			camera.Position = m_LiveCamera->GetPosition();
			camera.Yaw = m_LiveCamera->GetYaw();
			camera.Pitch = m_LiveCamera->GetPitch();
			camera.FOV = m_LiveCamera->GetFOV();
			camera.NearClip = m_LiveCamera->GetNearPlane();
			camera.FarClip = m_LiveCamera->GetFarPlane();
			m_Cameras.push_back(camera);
			SaveCameras();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Render resolution (reference only -- independent of the editor viewport)");
		{
			static const char* presetLabels[] = { "1080p", "1440p", "4K", "Custom" };
			static const glm::uvec2 presetResolutions[] = { { 1920u, 1080u }, { 2560u, 1440u }, { 3840u, 2160u } };
			int presetIndex = 3;
			for (int p = 0; p < 3; ++p)
			{
				if (m_Resolution == presetResolutions[p])
				{
					presetIndex = p;
					break;
				}
			}
			if (ImGui::Combo("Preset", &presetIndex, presetLabels, 4) && presetIndex < 3)
				m_Resolution = presetResolutions[presetIndex];
		}
		ImGui::InputScalar("Width", ImGuiDataType_U32, &m_Resolution.x);
		ImGui::InputScalar("Height", ImGuiDataType_U32, &m_Resolution.y);
		m_Resolution.x = glm::max(m_Resolution.x, 1u);
		m_Resolution.y = glm::max(m_Resolution.y, 1u);

		ImGui::Separator();
		ImGui::InputScalar("Warm-up frames", ImGuiDataType_U32, &m_WarmupFrames);
		ImGui::InputScalar("Sample frames", ImGuiDataType_U32, &m_SampleFrames);

		ImGui::Separator();
		if (ImGui::Button("Run benchmark") && !m_Cameras.empty() && m_ActiveSceneRenderer)
			StartRun();
		if (m_Cameras.empty())
			ImGui::TextDisabled("Save at least one camera first.");

		ImGui::End();
	}
}

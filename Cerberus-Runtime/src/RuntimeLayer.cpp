#include "pch.h"
#include "RuntimeLayer.h"
#include "Core/Application.h"
#include <imgui/imgui.h>
#include "ImGui/ImGuiUtils.h"
namespace cerberus
{
	RuntimeLayer::RuntimeLayer()
		: Layer("RuntimeLayer")
	{
	}

	void RuntimeLayer::OnAttach()
	{
		auto resolution = Application::Get().GetWindow()->GetSize();
		m_Scene = AssetManager::GetAsset<Scene>("Resources/Scenes/First Scene.cscene");
		SceneRendererSpecification rendererSpec;
		rendererSpec.SwapChainTarget = false;
		rendererSpec.Resolution = { resolution.first, resolution.second };
		m_SceneRenderer = CreateRef<SceneRenderer>(m_Scene, rendererSpec);
		m_Camera = FlyingCamera(45.f, resolution.first / resolution.second, 0.1f, 1000.f);
	}

	void RuntimeLayer::OnDetach()
	{
	}
	
	void RuntimeLayer::OnImGuiRender()
	{
		UI_DockSpace();
		UI_Viewport();
		m_SceneRenderer->OnImGuiRender();
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
		m_Camera.OnEvent(event);
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(RuntimeLayer::OnWindowResize));
		dispatcher.Dispatch<KeyPressedEvent>(BIND_EVENT_FN(RuntimeLayer::OnKeyPress));
	}

	void RuntimeLayer::OnUpdate(Timestep ts)
	{
		m_Camera.OnUpdate(ts);

		SceneRendererCamera sceneRendererCamera;
		sceneRendererCamera.Camera = m_Camera;
		sceneRendererCamera.ViewMatrix = m_Camera.GetViewMatrix();
		sceneRendererCamera.ViewPosition = m_Camera.GetPosition();
		sceneRendererCamera.Target = m_Camera.GetTarget();
		sceneRendererCamera.Near = m_Camera.GetNearPlane();
		sceneRendererCamera.Far = m_Camera.GetFarPlane();
		m_Scene->OnUpdate(m_SceneRenderer, sceneRendererCamera, ts);
	}

	void RuntimeLayer::OpenScene()
	{
		std::string filepath = FileDialogs::OpenFile("Cerberus Scene (*.cscene)\0*.cscene\0");
		if (!filepath.empty())
		{
			std::filesystem::path relativePath = FileSystem::GetRelativePathFromAbsolute(filepath);
			m_Scene = AssetManager::GetAsset<Scene>(relativePath);
			m_Scene->SetName(relativePath.stem().string());
			m_SceneRenderer->SetSceneContext(m_Scene);
		}
	}

	bool RuntimeLayer::OnWindowResize(WindowResizeEvent& event)
	{
		m_SceneRenderer->SetViewportSize(event.GetWidth(), event.GetHeight());
		m_Camera.SetViewportSize(event.GetWidth(), event.GetHeight());
		return false;
	}

	bool RuntimeLayer::OnKeyPress(KeyPressedEvent& event)
	{
		bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		//bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		if(event.GetKeyCode() == Key::O && control)
			OpenScene();

		return false;
	}

	void RuntimeLayer::UI_Viewport()
	{
		////////////////////////////////////////////////
		//// Viewport
		////////////////////////////////////////////////
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Viewport");
		{
			m_ViewportPanelMouseOver = ImGui::IsWindowHovered();

			Application::Get().BlockEvents(!(m_ViewportPanelMouseOver));

			ImVec2 viewportSize = ImGui::GetContentRegionAvail();
			m_SceneRenderer->SetViewportSize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);

			m_Camera.SetViewportSize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);

			// Render viewport image
			ui::Image(m_SceneRenderer->GetFinalPassImage(), viewportSize, { 0, 1 }, { 1, 0 });
		}

		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::End();
	}
	void RuntimeLayer::UI_DockSpace()
	{
		static bool opt_fullscreen_persistant = true;
		static ImGuiDockNodeFlags opt_flags = ImGuiDockNodeFlags_None;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static bool doockSpaceOpen = true;

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();


		io.ConfigWindowsResizeFromEdges = io.BackendFlags & ImGuiBackendFlags_HasMouseCursors;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		// When using ImGuiDockNodeFlags_PassthruDockspace, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
		//if (opt_flags & ImGuiDockNodeFlags_PassthruDockspace)
		//	window_flags |= ImGuiWindowFlags_NoBackground;
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow()->GetNativeWindow());
		bool isMaximized = (bool)glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, isMaximized ? ImVec2(6.0f, 6.0f) : ImVec2(1.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::Begin("DockSpace Demo", &doockSpaceOpen, window_flags);
		ImGui::PopStyleColor(); // MenuBarBg
		ImGui::PopStyleVar(2);

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		//ImGui::SetCursorPosY(titlebarHeight + ImGui::GetCurrentWindow()->WindowPadding.y);

		// Dockspace
		float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), opt_flags);
		}

		style.WindowMinSize.x = minWinSizeX;
	}
}

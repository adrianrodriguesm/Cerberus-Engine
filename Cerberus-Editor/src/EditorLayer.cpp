#include "pch.h"
#include "EditorLayer.h"
#include "Core/Application.h"
#include <imgui/imgui.h>
#include "ImGui/ImGuiUtils.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ScenePropertiesPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/AssetEditor.h"
namespace cerberus
{
	EditorLayer::EditorLayer()
		: Layer("EditorLayer")
	{
	}

	void EditorLayer::OnAttach()
	{
		AssetEditorManager::Init();
		auto resolution = Application::Get().GetWindow()->GetSize();
		m_Scene = AssetManager::GetAsset<Scene>("Resources/Scenes/First Scene.cscene");
		SceneRendererSpecification rendererSpec;
		rendererSpec.SwapChainTarget = false;
		rendererSpec.Resolution = { resolution.first, resolution.second };
		m_SceneRenderer = CreateRef<PathTracerSceneRenderer>(m_Scene, rendererSpec);
		m_Camera = FlyingCamera(45.f, resolution.first / resolution.second, 0.1f, 1000.f);
		

		Ref<ScenePropertiesPanel> scenePropertiesPanel = CreateRef<ScenePropertiesPanel>(m_Scene);
		Ref<SceneHierarchyPanel> sceneHierarchyPanel = CreateRef<SceneHierarchyPanel>(m_Scene);
		Ref<ContentBrowserPanel> contentBrowserPanel = CreateRef<ContentBrowserPanel>();
		sceneHierarchyPanel->SetSelectionChangedCallback(m_SelectionChangedCallback);
		m_EditorPanelManager = CreateRef<EditorPanelManager>();
		m_EditorPanelManager->AddPanel("ScenePropertiesPanel", { true, scenePropertiesPanel });
		m_EditorPanelManager->AddPanel("SceneHierarchyPanel", { true, sceneHierarchyPanel });
		m_EditorPanelManager->AddPanel("ContentBrowserPanel", { true, contentBrowserPanel });
	}

	void EditorLayer::OnDetach()
	{
		m_EditorPanelManager->Shutdown();
		AssetEditorManager::Shutdown();
	}
	
	void EditorLayer::OnImGuiRender()
	{
		UI_DockSpace();
		UI_RenderTittleBar();
		UI_Viewport();
		m_SceneRenderer->OnImGuiRender();
		m_EditorPanelManager->OnImGuiRender();
		AssetEditorManager::OnImGuiRender();
	}

	void EditorLayer::OnEvent(Event& event)
	{
		m_Camera.OnEvent(event);
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(EditorLayer::OnWindowResize));
		dispatcher.Dispatch<KeyPressedEvent>(BIND_EVENT_FN(EditorLayer::OnKeyPress));
	}

	void EditorLayer::OnUpdate(Timestep ts)
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

	void EditorLayer::SelectEntity(const Entity& entity)
	{
		m_SelectedEntity = entity;
		m_EditorPanelManager->SetSelectionContext(entity);
	}

	void EditorLayer::NewScene()
	{
		m_Scene = CreateRef<Scene>("New Scene");
		m_SceneRenderer->SetSceneContext(m_Scene);
		SelectEntity({});
		m_EditorPanelManager->SetSceneContext(m_Scene);
	}

	void EditorLayer::OpenScene()
	{
		std::string filepath = FileDialogs::OpenFile("Cerberus Scene (*.cscene)\0*.cscene\0");
		if (!filepath.empty())
		{
			std::filesystem::path relativePath = FileSystem::GetRelativePathFromAbsolute(filepath);
			m_Scene = AssetManager::GetAsset<Scene>(relativePath);
			m_Scene->SetName(relativePath.stem().string());
			m_SceneRenderer->SetSceneContext(m_Scene);
			SelectEntity({});
			m_EditorPanelManager->SetSceneContext(m_Scene);

		}
	}

	void EditorLayer::OpenScene(const Ref<Scene>& scene)
	{
		m_Scene = scene;
		m_SceneRenderer->SetSceneContext(m_Scene);
		SelectEntity({});
		m_EditorPanelManager->SetSceneContext(m_Scene);
	}

	void EditorLayer::SaveSceneAs()
	{
		std::string filepath = FileDialogs::SaveFile("Cerberus Scene (*.cscene)\0*.cscene\0");
		if (!filepath.empty())
		{
			std::filesystem::path relativePath = FileSystem::GetRelativePathFromAbsolute(filepath);

			if (AssetManager::GetMetadata(relativePath).IsValid())
			{
				AssetImporter::Serialize(m_Scene);
			}
			else
			{
				bool succeded = AssetManager::LoadMemoryAsset(m_Scene, relativePath.filename().string(), relativePath.parent_path().string());
				if (succeded)
					ContentBrowserPanel::Get().Refresh();
			}

		}
	}

	void EditorLayer::SaveScene()
	{
		AssetImporter::Serialize(m_Scene);
	}

	bool EditorLayer::OnWindowResize(WindowResizeEvent& event)
	{
		m_SceneRenderer->SetViewportSize(event.GetWidth(), event.GetHeight());
		m_Camera.SetViewportSize((float)event.GetWidth(), (float)event.GetHeight());
		return false;
	}

	bool EditorLayer::OnKeyPress(KeyPressedEvent& event)
	{
		bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		switch (event.GetKeyCode())
		{
		// Scene keys
		case Key::N:
		{
			if (control)
				NewScene();
			break;
		}
		case Key::O:
		{
			if (control)
				OpenScene();
			break;
		}
		case Key::S:
		{
			if (control)
			{
				if (shift)
					SaveSceneAs();
				else
					SaveScene();
			}
			break;
		}
		// Gizmos
		case Key::Q:
			m_GizmoType = -1;
			break;
		case Key::W:
			m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
			break;
		case Key::E:
			m_GizmoType = ImGuizmo::OPERATION::ROTATE;
			break;
		case Key::R:
			m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;
		}
		return false;
	}

	void EditorLayer::UI_RenderTittleBar()
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exits")) { Application::Get().Close(); }
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) { NewScene(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) { OpenScene(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) { SaveScene(); }
				ImGui::Separator();
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
	}
	void EditorLayer::UI_Viewport()
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

			// ImGuizmo
			if (m_SelectedEntity && m_GizmoType != -1)
			{
				ImGuizmo::SetOrthographic(false);
				// 1 Call in order draw a gizmo to the window
				ImGuizmo::SetDrawlist();
				// 2 Set Viewport
				float windowWidth = (float)ImGui::GetWindowWidth();
				float windowHeight = (float)ImGui::GetWindowHeight();
				ImVec2 windowPosition = ImGui::GetWindowPos();
				ImGuizmo::SetRect(windowPosition.x, -windowPosition.y, windowWidth, windowHeight);
				// 3 Draw Gizmo
				// -- Entity Transfom
				auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();
				glm::mat4 transform = tc.GetTransform();
				// Snap value
				glm::vec3 snap = { 0.f, 0.f, 0.f };
				if (Input::IsKeyPressed(Key::LeftControl))
				{
					if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
						snap = { 45.f, 45.f, 45.f };
					else
						snap = { 0.5f, 0.5f, 0.5f }; // 0.5m for translation and scale
				}

				// Render the gizmo
				ImGuizmo::Manipulate(glm::value_ptr(m_Camera.GetViewMatrix()), 
					glm::value_ptr(m_Camera.GetProjection()),
					(ImGuizmo::OPERATION)m_GizmoType, 
					ImGuizmo::LOCAL /* Local space */, 
					glm::value_ptr(transform),
					nullptr, 
					glm::value_ptr(snap));

				if (ImGuizmo::IsUsing())
				{
					glm::vec3 translation, rotation, scale;
					math::DecomposeTransform(transform, translation, rotation, scale);
					tc.SetTranslation(translation);
					tc.SetRotation(tc.GetRotation() + glm::degrees(rotation) - tc.GetRotation());
					tc.SetScale(scale);
				}
			}
			// Drag scene into the viewport
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload("asset_payload"))
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					Ref<Scene> scene = AssetManager::GetAsset<Scene>(assetHandle);
					if (scene && scene->GetAssetType() == AssetType::Scene)
						OpenScene(scene);
				}
				ImGui::EndDragDropTarget();
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::End();
	}
	void EditorLayer::UI_DockSpace()
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
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
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

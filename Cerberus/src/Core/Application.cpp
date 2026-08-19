#include "pch.h"
#include "Core/Application.h"
#include "Core/Timer.h"
#include "Renderer/Renderer.h"
#include "Renderer/ImGuiRenderer.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/RenderQueueManager.h"
#include "Asset/AssetManager.h"
#include "Utils/FileSystem.h"

#include <imgui/imgui.h>
namespace cerberus
{
	Application* Application::s_App = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		CORE_ASSERT(!s_App, "Aplication already exits!");
		s_App = this;

		WindowSpecification windowSpec;
		windowSpec.Title = m_Specification.Name;
		windowSpec.Fullscreen = m_Specification.Fullscreen;
		windowSpec.Width = m_Specification.WindowWidth;
		windowSpec.Height = m_Specification.WindowHeight;
		m_Window = CreateRef<Window>(windowSpec);
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
		Log::Init();
		m_Window->Init();

		FileSystem::Init(m_Specification.EngineRoot, m_Specification.ProjectRoot);

		Renderer::Init();
		AssetManager::Init();
	}

	Application::~Application()
	{
		for (Layer* layer : m_LayerStack)
		{
			layer->OnDetach();
			delete layer;
		}

		// Loaded assets can own ResourceManager handles (for example generated
		// environment maps), so release them while the renderer is still alive.
		AssetManager::Shutdown();
		Renderer::Shutdown();
		m_Window->Shutdown();
		
	}

	void Application::BlockEvents(bool allowEvents)
	{
		m_BlockEvents = allowEvents;
	}

	void Application::OnImGuiRender()
	{
		for (Layer* layer : m_LayerStack)
			layer->OnImGuiRender();
	}

	void Application::Run()
	{
		RenderQueueManager::ExecuteRenderQueue();
		Timer timer;
		
		while (m_Running)
		{
			m_Window->ProcessEvents();

			if (m_Minimized)
				continue;

			m_TimeStep = timer.ElapsedAndReset();
			Renderer::BeginFrame();
			{
				ImGuiRenderer::Render();
				
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(m_TimeStep);

			}
			Renderer::EndFrame();
			
			auto swapChain = m_Window->GetSwapchain();
			swapChain->UpdateNextImageToRender();
			{
				RenderQueueManager::ExecuteRenderQueue();
			}
			swapChain->Present();
		}
		
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));

		if (m_BlockEvents)
		{
			ImGuiIO& io = ImGui::GetIO();
			event.m_IsHandled |= event.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			event.m_IsHandled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
			event.m_IsHandled |= event.IsInCategory(EventCategoryMouseButton) & io.WantCaptureKeyboard;
		}

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (event.m_IsHandled)
				break;
			(*it)->OnEvent(event);
		}
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;
	}

	bool Application::OnWindowResize(WindowResizeEvent& event)
	{
		const uint32_t width = event.GetWidth(), height = event.GetHeight();
		if (width == 0 || height == 0)
		{
			m_Minimized = true;
			return false;
		}
		m_Minimized = false;

		return false;
	}

	bool Application::OnWindowClose(WindowCloseEvent& event)
	{
		m_Running = false;
		return true;
	}

}

#include "pch.h"
#include "Core/Application.h"
#include "Core/Timer.h"
#include "Renderer/Renderer.h"
#include "Asset/AssetManager.h"

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
		Log::init();
		m_Window->Init();
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
		Renderer::ExecuteRenderQueue();
		Timer timer;
		
		while (m_Running)
		{
			m_Window->ProcessEvents();

			if (m_Minimized)
				continue;

			m_TimeStep = timer.ElapsedAndReset();
			Renderer::BeginFrame();
			{
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(m_TimeStep);

				Renderer::RenderImGui();
			}
			Renderer::EndFrame();
			
			auto swapChain = m_Window->GetSwapchain();
			swapChain->UpdateNextImageToRender();
			{
				Renderer::ExecuteRenderQueue();
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
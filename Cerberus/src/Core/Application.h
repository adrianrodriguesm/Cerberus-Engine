#pragma once
#include "Window.h"
#include "Core\LayerStack.h"
#include "Events/ApplicationEvent.h"
int main(int argc, char** argv);
namespace cerberus
{
	struct ApplicationSpecification
	{
		std::string Name = "Cerberus";
		uint32_t WindowWidth = 1600, WindowHeight = 900;
		bool Fullscreen = false;
	};
	class Application
	{
	public:
		void Run();

		void OnEvent(Event& event);

		void Close();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);
		const Timestep& GetTimestep() const { return m_TimeStep; }
		
		const Ref<Window> GetWindow() const { return m_Window; }

		static Application& Get() { return *s_App; }

		bool OnWindowResize(WindowResizeEvent& event);
		bool OnWindowClose(WindowCloseEvent& event);
		void OnImGuiRender();

		void BlockEvents(bool allowEvents);
	protected:
		Application(const ApplicationSpecification& specification);
		virtual ~Application();
		friend int ::main(int argc, char** argv);
	private:
		static Application* s_App;
		ApplicationSpecification m_Specification;
		Ref<Window> m_Window;
		LayerStack m_LayerStack;
		Timestep m_TimeStep;		
		bool m_Running = true;
		bool m_Minimized = false;
		bool m_BlockEvents = false;
	};

	Application* CreateApplication();
}
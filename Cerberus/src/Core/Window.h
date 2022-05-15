#pragma once
#include <Renderer/Swapchain.h>
#include "Events/Event.h"
#include "Renderer/RendererContext.h"
namespace cerberus 
{
	struct WindowSpecification
	{
		std::string Title = "cerberus";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Fullscreen = false;
	};

	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;
		Window(const WindowSpecification& spec);
		~Window();

		void Init();
		void Shutdown();

		void ProcessEvents();
		void Maximize();
		void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }

		void SetCursor(bool enable);

		inline uint32_t GetWidth() const { return m_Data.Width; }
		inline uint32_t GetHeight() const { return m_Data.Height; }
		std::pair<uint32_t, uint32_t> GetSize() const  { return { m_Data.Width, m_Data.Height }; }

		Ref<Swapchain> GetSwapchain() const { return m_Swapchain; }
		static Ref<Swapchain> GetCurrentSwapchain();
		inline GLFWwindow* GetNativeWindow() { return m_Window; }
		Ref<RendererContext> GetRendererContext() { return m_RendererContext; }
	private:
		GLFWwindow* m_Window;
		WindowSpecification m_Specification;
		Ref<Swapchain> m_Swapchain;
		Ref<RendererContext> m_RendererContext;

		struct WindowData
		{
			std::string Title;
			uint32_t Width, Height;
			EventCallbackFn EventCallback;
		} m_Data;
	};
}



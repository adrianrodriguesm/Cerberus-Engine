#include "pch.h"
#include "Window.h"
#include "Renderer/Core/RendererContext.h"
#include "Core/Application.h"


#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include <stb/include/stb_image.h>
namespace cerberus
{
	static void GLFWErrorCallback(int error, const char* description)
	{
		NV_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
	}

	static bool s_GLFWInitialized = false;

	Window::Window(const WindowSpecification& spec)
		: m_Specification(spec)
	{

		
	}



	Window::~Window()
	{
		//Destroy GLFW Window and stop GLFW
		//glfwDestroyWindow(m_Window);
		//glfwTerminate();
	}
	void Window::Init()
	{
		NV_CORE_INFO("Creating window {0} ({1}, {2})", m_Specification.Title, m_Specification.Width, m_Specification.Height);
		
		m_Data.Title = m_Specification.Title;
		m_Data.Width = m_Specification.Width;
		m_Data.Height = m_Specification.Height;

		// Check if glfw was already initialized
		if (!s_GLFWInitialized)
		{
			int success = glfwInit();
			CORE_ASSERT(success, "Could not intialize GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLFWInitialized = true;
		}
		// Set GLFW TO NOT WORK WITH OPENGL
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		if (m_Specification.Fullscreen)
		{
			GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
			m_Window = glfwCreateWindow(mode->width, mode->height, m_Specification.Title.c_str(), primaryMonitor, nullptr);
		}
		else
		{
			m_Window = glfwCreateWindow((int)m_Specification.Width, (int)m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);
		}

		if (!m_Window)
		{
			glfwTerminate();
			CORE_ASSERT(false,"Windows not created!");
		}

		// Init Render Context
		m_RendererContext = CreateRef<RendererContext>();
		m_RendererContext->Init();

		VkInstance vulkanInstance = RendererContext::GetInstance();
		Ref<Device> device = RendererContext::GetCurrentDevice();
		m_Swapchain = CreateRef<Swapchain>(vulkanInstance, device);
		m_Swapchain->InitSurface(m_Window);
		NV_CORE_INFO("Surface created with success");
		NV_CORE_INFO("...");
		m_Swapchain->InitSwapchain();
		NV_CORE_INFO("Vulkan Swap Chain created with success");

		//glfwMaximizeWindow(m_Window);
		glfwSetWindowUserPointer(m_Window, &m_Data);

		/////////////////////////////////////////////////
		// Set GLFW callbacks
		/////////////////////////////////////////////////
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			WindowResizeEvent event((uint32_t)width, (uint32_t)height);
			data.EventCallback(event);
			data.Width = width;
			data.Height = height;
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			WindowCloseEvent event;
			data.EventCallback(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			switch (action)
			{
			case GLFW_PRESS:
			{
				KeyPressedEvent event((KeyCode)key, 0);
				data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				KeyReleasedEvent event((KeyCode)key);
				data.EventCallback(event);
				break;
			}
			case GLFW_REPEAT:
			{
				KeyPressedEvent event((KeyCode)key, 1);
				data.EventCallback(event);
				break;
			}
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, uint32_t codepoint)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			KeyTypedEvent event((KeyCode)codepoint);
			data.EventCallback(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			switch (action)
			{
			case GLFW_PRESS:
			{
				MouseButtonPressedEvent event(button);
				data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				MouseButtonReleasedEvent event(button);
				data.EventCallback(event);
				break;
			}
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			MouseMovedEvent event((float)x, (float)y);
			data.EventCallback(event);
		});

		// Update window size to actual size
		{
			int width, height;
			glfwGetWindowSize(m_Window, &width, &height);
			m_Data.Width = width;
			m_Data.Height = height;
		}

		// Set icon
		{
			GLFWimage icon;
			int channels;
			icon.pixels = stbi_load("Resources/Editor/Cerberus-Icon.png", &icon.width, &icon.height, &channels, 4);
			glfwSetWindowIcon(m_Window, 1, &icon);
			stbi_image_free(icon.pixels);
		}

	}
	void Window::Shutdown()
	{
		m_Swapchain->Shutdown();
		//Destroy GLFW Window and stop GLFW
		glfwDestroyWindow(m_Window);
		glfwTerminate();
		s_GLFWInitialized = false;

		m_RendererContext->Shutdown();
	}
	void Window::ProcessEvents()
	{
		glfwPollEvents();
	}
	Ref<Swapchain> Window::GetCurrentSwapchain()
	{
		return  Application::Get().GetWindow()->GetSwapchain();;
	}

	void Window::Maximize()
	{
		glfwMaximizeWindow(m_Window);
	}
	void Window::SetCursor(bool enable)
	{
		(!enable) ? glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED) : glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}


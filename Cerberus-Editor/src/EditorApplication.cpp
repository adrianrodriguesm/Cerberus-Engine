#include "pch.h"
#include "EditorLayer.h"
#include "EntryPoint.h"

namespace cerberus
{
	class EditorApplication : public Application
	{
	public:
		EditorApplication(const ApplicationSpecification& specification)
			: Application(specification)
		{
			//GetWindow()->Maximize();
			PushLayer(new EditorLayer());
		}
		~EditorApplication() {}

	};

	Application* CreateApplication()
	{
		ApplicationSpecification specification;
		specification.Fullscreen = false;
		return new EditorApplication(specification);
	}
}


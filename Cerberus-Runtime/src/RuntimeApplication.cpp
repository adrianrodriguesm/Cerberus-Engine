#include "pch.h"
#include "RuntimeLayer.h"
#include "EntryPoint.h"

namespace cerberus
{
	class RuntimeApplication : public Application
	{
	public:
		RuntimeApplication(const ApplicationSpecification& specification)
			: Application(specification)
		{
			GetWindow()->Maximize();
			PushLayer(new RuntimeLayer());
		}
		~RuntimeApplication() {}

	};

	Application* CreateApplication()
	{
		ApplicationSpecification specification;
		specification.Fullscreen = false;
		return new RuntimeApplication(specification);
	}
}


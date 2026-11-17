#include "pch.h"
#include "EditorLayer.h"
#include "EntryPoint.h"

namespace cerberus
{
	class EditorApplication : public Application
	{
	public:
		EditorApplication(const ApplicationSpecification& specification,
			std::string benchmarkScenePath, std::string benchmarkOutputCSVPath)
			: Application(specification)
		{
			//GetWindow()->Maximize();
			PushLayer(new EditorLayer(std::move(benchmarkScenePath), std::move(benchmarkOutputCSVPath)));
		}
		~EditorApplication() {}

	};

	Application* CreateApplication(int argc, char** argv)
	{
		ApplicationSpecification specification;
		specification.Fullscreen = false;
		specification.EngineRoot = "Cerberus";
		specification.ProjectRoot = "Cerberus-Editor";

		// --benchmark <scene> <out.csv>: runs every saved camera for that
		// scene headlessly (no manual Run-benchmark click) and exits once the
		// CSV is written -- see EditorLayer's constructor and OnAttach/OnUpdate.
		std::string benchmarkScenePath, benchmarkOutputCSVPath;
		for (int i = 1; i + 2 < argc; ++i)
		{
			if (std::string(argv[i]) == "--benchmark")
			{
				benchmarkScenePath = argv[i + 1];
				benchmarkOutputCSVPath = argv[i + 2];
				break;
			}
		}

		return new EditorApplication(specification, std::move(benchmarkScenePath), std::move(benchmarkOutputCSVPath));
	}
}


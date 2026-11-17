#include "Core/Application.h"

extern cerberus::Application* cerberus::CreateApplication(int argc, char** argv);
/////////////////////////////////////////////////////////////////////////// MAIN
int main(int argc, char** argv)
{
	auto app = cerberus::CreateApplication(argc, argv);
	// RUN
	app->Run();
	// SHUTDOWN
	delete app;
	exit(EXIT_SUCCESS);
}
/////////////////////////////////////////////////////////////////////////// END
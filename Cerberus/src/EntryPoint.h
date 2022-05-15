#include "Core/Application.h"

extern cerberus::Application* cerberus::CreateApplication();
/////////////////////////////////////////////////////////////////////////// MAIN
int main(int argc, char** argv)
{
	auto app = cerberus::CreateApplication();
	// RUN
	app->Run();
	// SHUTDOWN
	delete app;
	exit(EXIT_SUCCESS);
}
/////////////////////////////////////////////////////////////////////////// END
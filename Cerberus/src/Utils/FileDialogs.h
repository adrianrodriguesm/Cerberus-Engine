#pragma once

namespace cerberus
{
	class FileDialogs
	{
	public:
		static std::string OpenFile(const char* filter = "All\0*.*\0");
		static std::string SaveFile(const char* filter);
	};
}
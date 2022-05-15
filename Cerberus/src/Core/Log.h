#pragma once
// Ignored the warnings inside the library
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

namespace cerberus
{
	class Log
	{
	public:
		static void init();
		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
	};

}


// Core log macros
#define NV_CORE_TRACE(...)    ::cerberus::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define NV_CORE_INFO(...)     ::cerberus::Log::GetCoreLogger()->info(__VA_ARGS__)
#define NV_CORE_WARN(...)     ::cerberus::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define NV_CORE_ERROR(...)    ::cerberus::Log::GetCoreLogger()->error(__VA_ARGS__)
#define NV_CORE_CRITICAL(...) ::cerberus::Log::GetCoreLogger()->critical(__VA_ARGS__)

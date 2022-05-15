#pragma once
#include <chrono>
namespace cerberus
{
	class Timer
	{
	public:
		Timer()
			: m_Start(std::chrono::high_resolution_clock::now()) {}

		inline void Reset()
		{
			m_Start = std::chrono::high_resolution_clock::now();
		}
		// Return the elapse time in seconds and reset the timer
		inline float ElapsedAndReset()
		{
			std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();
			float elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_Start).count() * 0.001f * 0.001f * 0.001f;
			m_Start = now;
			return elapsed;
		}
		inline float ElapsedSeconds()
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - m_Start).count() * 0.001f * 0.001f * 0.001f;
		}
		inline float ElapsedMilliseconds()
		{
			return ElapsedSeconds() * 1000.f;
		}
	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
	};
}
#pragma once
namespace cerberus
{
	class Timestep
	{
	public:
		Timestep(float time = 0.0f)
			: m_Time(time)
		{};


		operator float() { return m_Time; }

		inline float GetSeconds() { return m_Time; }
		inline float GetMilliSeconds() { return m_Time * 1000.f; }
	private:
		float m_Time;
	};
}
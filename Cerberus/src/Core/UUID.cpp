#include "pch.h"
#include "UUID.h"

#include <random>
namespace cerberus
{
	// 64BIT
	static std::random_device s_RandomDevice;
	static std::mt19937_64 eng(s_RandomDevice());
	static std::uniform_int_distribution<uint64_t> s_UniformDistribution;
	// 16BIT
	static std::mt19937 eng16(s_RandomDevice());
	static std::uniform_int_distribution<uint16_t> s_UniformDistribution16;

	UUID::UUID()
		: m_UUID(s_UniformDistribution(eng))
	{
	}

	UUID::UUID(uint64_t uuid)
		: m_UUID(uuid)
	{
	}

	UUID::UUID(const UUID& other)
		: m_UUID(other.m_UUID)
	{
	}

	UUID_16::UUID_16()
		: m_UUID(s_UniformDistribution16(eng16))
	{
	}

	UUID_16::UUID_16(uint16_t uuid)
		: m_UUID(uuid)
	{
	}

	UUID_16::UUID_16(const UUID_16& other)
		: m_UUID(other.m_UUID)
	{
	}
}
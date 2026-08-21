#pragma once

#include "Core/Base.h"

#include <cstdint>
#include <string>

namespace cerberus
{
	class TransientCommandExecutor;
	struct EnvironmentMap;

	struct EnvironmentMapBakeSpecification
	{
		uint32_t EnvironmentResolution = 1024;
		uint32_t IrradianceResolution = 32;
		uint32_t IrradianceSampleCount = 1024;
	};

	// Offline/setup GPU baker. This is deliberately not a frame render pass:
	// environment assets are generated on demand and then reused by render passes.
	class EnvironmentMapBaker final
	{
	public:
		explicit EnvironmentMapBaker(TransientCommandExecutor& commandExecutor);

		Ref<EnvironmentMap> Bake(
			const std::string& filepath,
			const EnvironmentMapBakeSpecification& specification) const;

	private:
		TransientCommandExecutor* m_CommandExecutor = nullptr;
	};
}

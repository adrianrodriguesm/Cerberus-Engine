#include "pch.h"
#include "EnvironmentMap.h"
#include "Renderer/ResourceManager.h"

namespace cerberus
{
	EnvironmentMap::~EnvironmentMap()
	{
		if (OwnsGPUResources)
		{
			if (EquirectangularTex.IsValid())
				ResourceManager::DestroyTexture2D(EquirectangularTex);

			if (RadianceMap.IsValid())
				ResourceManager::DestroyTextureCube(RadianceMap);

			if (IrradianceMap.IsValid() && !(IrradianceMap == RadianceMap))
				ResourceManager::DestroyTextureCube(IrradianceMap);
		}

		NV_CORE_WARN("Environment Map {0} released", DebugName);
	}
}

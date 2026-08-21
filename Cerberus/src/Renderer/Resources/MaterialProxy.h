#pragma once

#include "Renderer/Material.h"

namespace cerberus
{
	// RenderScene-owned, render-friendly representation of a MaterialAsset.
	//
	// This intentionally owns neither a BindGroup nor Texture2D resources. RenderScene
	// owns the shared texture table and resolves asset handles into its indices; concrete
	// renderers upload this compact snapshot into their GPU material buffers.
	static constexpr uint32_t InvalidMaterialProxyIndex = 0xFFFFFFFFu;

	struct MaterialProxy
	{
		AssetHandle SourceMaterialAsset = 0;

		// Currently this is the same as the RenderScene material proxy table index.
		// Later this can diverge if the renderer compacts, sorts, or aliases GPU material entries.
		uint32_t GPUMaterialIndex = InvalidMaterialProxyIndex;

		GPUMaterialData GPUData{};
	};
}

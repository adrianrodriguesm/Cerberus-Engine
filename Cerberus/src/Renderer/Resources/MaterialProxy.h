#pragma once

#include "Renderer/Material.h"

namespace cerberus
{
	// RenderScene-owned, render-friendly representation of a MaterialAsset.
	//
	// This intentionally does not own a BindGroup or resolved Texture2D resources yet.
	// The next material phases will resolve texture asset handles into bindless texture
	// indices and upload MaterialData to a GPU material table. Keeping this proxy as a
	// compact CPU-side snapshot gives both raster and ray tracing a stable material index.
	static constexpr uint32_t InvalidMaterialProxyIndex = 0xFFFFFFFFu;

	struct MaterialProxy
	{
		AssetHandle SourceMaterialAsset = 0;

		// For now, this is the same as the RenderScene material proxy table index.
		// Later this can diverge if the renderer compacts, sorts, or aliases GPU material entries.
		uint32_t GPUMaterialIndex = InvalidMaterialProxyIndex;

		GPUMaterialData GPUData{};
	};
}

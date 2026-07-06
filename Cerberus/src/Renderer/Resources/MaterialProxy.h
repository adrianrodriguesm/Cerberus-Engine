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
		GPUMaterialData GPUData{};
	};
}

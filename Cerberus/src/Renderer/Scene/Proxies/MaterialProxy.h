#pragma once
#include "Asset/Asset.h"
#include "Asset/Assets/MaterialAsset.h"
namespace cerberus
{
	// RenderScene-owned, render-friendly representation of a MaterialAsset.
	//
	// This intentionally owns neither a BindGroup nor Texture2D resources. RenderScene
	// owns the shared texture table and resolves asset handles into its indices; concrete
	// renderers upload this compact snapshot into their GPU material buffers.
	static constexpr uint32_t InvalidMaterialProxyIndex = 0xFFFFFFFFu;

	// Compact material representation intended for future raster/RT material tables.
	// Texture indices are bindless table indices and are provided by RenderScene/MaterialProxy.
	struct GPUMaterialData
	{
		glm::vec4 AlbedoColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
		// x = Roughness, y = Metalness, z = Emission, w = UseNormalMap
		glm::vec4 Parameters = glm::vec4(0.4f, 0.0f, 0.0f, 0.0f);
		glm::uvec4 TextureIndices = glm::uvec4(InvalidMaterialTextureIndex);
		// x = emissive texture index (tints AlbedoColor.rgb * Parameters.z as its
		// own colour instead of reusing the albedo texture), y = linear opacity
		// texture index (red channel), z/w reserved.
		glm::uvec4 TextureIndices2 = glm::uvec4(InvalidMaterialTextureIndex);
		// x = packed metal-roughness (TextureIndices.z/.w point at one texture;
		// read its G as roughness and B as metalness instead of each map's own R
		// -- see docs/benchmarks/bistro-inspection.md), y = reconstruct normal-map Z
		// from XY instead of sampling it (BC5/2-channel normal maps store no Z).
		// Both default to 0 so every material imported before this field existed
		// keeps its exact current (unpacked, stored-Z) behavior.
		glm::vec4 Flags = glm::vec4(0.0f);
		// x = cutoff, y = masked, z = two-sided, w reserved.
		glm::vec4 AlphaParameters = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);
	};
	static_assert(sizeof(GPUMaterialData) == 96, "GPUMaterialData must match GLSL std430");

	struct MaterialProxy
	{
		AssetHandle SourceMaterialAsset = 0;

		// Currently this is the same as the RenderScene material proxy table index.
		// Later this can diverge if the renderer compacts, sorts, or aliases GPU material entries.
		uint32_t GPUMaterialIndex = InvalidMaterialProxyIndex;

		GPUMaterialData GPUData{};
	};
}

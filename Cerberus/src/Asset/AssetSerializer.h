#pragma once
#include "Asset/Asset.h"
namespace cerberus
{
	class AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) = 0;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) = 0;
	};

	class TextureSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};

	class MaterialSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};

	class MeshSourceSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};

	class MeshSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};

	class EnvironmentMapSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override {};
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};

	class SceneSerializer : public AssetSerializer
	{
	public:
		virtual void Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) override;
		virtual bool TryToLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) override;
	};
}
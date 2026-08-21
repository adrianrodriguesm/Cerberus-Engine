#pragma once
#include "AssetEditor.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/TextureAsset.h"
namespace cerberus
{
	class MaterialPanel : public AssetEditorPanel
	{
	public:
		MaterialPanel();
		virtual ~MaterialPanel();
		virtual void Shutdown() override;
		virtual void SetAsset(const Ref<Asset>& asset) override;
	protected:
		virtual void Render() override;
		virtual void OnClose() override;
	private:
		struct TextureSlotEditResult
		{
			bool Changed = false;
			AssetHandle TextureAsset = 0;
		};

		Handle<Texture2D> GetTexturePreview(AssetHandle textureAsset, TextureColorSpace colorSpace);
		TextureSlotEditResult DrawTextureSlot(const char* id, AssetHandle textureAsset, TextureColorSpace colorSpace);
		void DestroyTexturePreviews();
		void CommitChanges();

		Handle<Texture2D> m_CheckerboardTex;
		std::unordered_map<AssetHandle, Handle<Texture2D>> m_SRGBTexturePreviews;
		std::unordered_map<AssetHandle, Handle<Texture2D>> m_LinearTexturePreviews;
		Ref<MaterialAsset> m_Material;
		bool m_IsDirty = false;
	};

	class MeshPanel : public AssetEditorPanel
	{
	public:
		MeshPanel();
		virtual ~MeshPanel();
		virtual void Shutdown() override {};
		virtual void SetAsset(const Ref<Asset>& asset) override;
	protected:
		virtual void Render() override;
		virtual void OnClose() override;
	private:
		Ref<MeshSource> m_MeshSource;
		std::array<char, 256> m_FilepathBuffer;
	};
}

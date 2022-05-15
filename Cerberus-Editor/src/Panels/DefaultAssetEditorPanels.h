#pragma once
#include "AssetEditor.h"
#include "Renderer/Mesh.h"
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
		Ref<Texture2D> m_CheckerboardTex;
		Ref<MaterialAsset> m_Material;
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
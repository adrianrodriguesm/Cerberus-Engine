#pragma once
#include "Backend/BindGroup.h"
#include "Renderer/Material.h"

namespace cerberus
{
	/** /
	class MaterialManager
	{
	public:
		static void Init();
		static void Shutdown();

		static Ref<Material> GetDefaultMaterial() { return m_DefaultMaterial; }

		static AssetHandle GetMaterial(Handle<BindGroup>);
		static 
	private:
		inline static std::unordered_map<AssetHandle, Handle<BindGroup>> m_MaterialMap;
		inline static Ref<Material> m_DefaultMaterial;
	};
	/**/

}
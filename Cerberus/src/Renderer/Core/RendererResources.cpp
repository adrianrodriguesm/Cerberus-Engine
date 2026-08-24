#include "pch.h"
#include "RendererResources.h"
#include "Renderer/Core/RenderQueueManager.h"
#include "Renderer/RHI/ResourceManager.h"

namespace cerberus
{
	struct RendererResourcesData
	{
		Handle<TextureCube> BlackCubeTexture;
		Handle<Texture2D> BlackTexture;
		Handle<Texture2D> WhiteTexture;
		Handle<Texture2D> BrdfLutTexture;

		Ref<MaterialAsset> DefaultMaterial;
		Ref<EnvironmentMap> EmptyEnvironment;
		Ref<ShaderLibrary> ShaderLibrary;
	};

	static RendererResourcesData* s_ResourcesData = nullptr;

	void RendererResources::Init()
	{
		CORE_ASSERT(!s_ResourcesData, "RendererResources already initialized");
		s_ResourcesData = new RendererResourcesData();

		// Shaders
		s_ResourcesData->ShaderLibrary = CreateRef<ShaderLibrary>();
		s_ResourcesData->ShaderLibrary->Load("engine://Shaders/Environment/EquirectangularToCubeMap.glsl", false);
		s_ResourcesData->ShaderLibrary->Load("engine://Shaders/Environment/EnvironmentMipFilter.glsl", false);
		s_ResourcesData->ShaderLibrary->Load("engine://Shaders/Environment/EnvironmentIrradiance.glsl", false);
		s_ResourcesData->ShaderLibrary->Load("Resources/Shaders/BRDF.glsl", false);

		// Texture factories such as the BRDF LUT reflect and execute compute shaders,
		// so shader creation must finish before default GPU textures are built.
		RenderQueueManager::ExecuteRenderQueue();

		// Textures
		s_ResourcesData->BlackCubeTexture = TextureFactory::CreateTextureCubeWithColor({ 0, 0, 0, 0 });
		s_ResourcesData->BlackTexture = TextureFactory::CreateTexture2DWithColor({ 0, 0, 0, 0 });
		s_ResourcesData->WhiteTexture = TextureFactory::CreateTexture2DWithColor({ 255, 255, 255, 255 });
		s_ResourcesData->BrdfLutTexture = TextureFactory::CreateBRDF();

		// EmptyEnvironment borrows renderer-owned fallback textures. It must never
		// release them; RendererResources owns their lifetime.
		s_ResourcesData->EmptyEnvironment = CreateRef<EnvironmentMap>();
		s_ResourcesData->EmptyEnvironment->EquirectangularTex = s_ResourcesData->BlackTexture;
		s_ResourcesData->EmptyEnvironment->IrradianceMap = s_ResourcesData->BlackCubeTexture;
		s_ResourcesData->EmptyEnvironment->RadianceMap = s_ResourcesData->BlackCubeTexture;
		s_ResourcesData->EmptyEnvironment->DebugName = "Empty Environment";
		s_ResourcesData->EmptyEnvironment->OwnsGPUResources = false;

		s_ResourcesData->DefaultMaterial = CreateRef<MaterialAsset>("Default PBR");
		s_ResourcesData->DefaultMaterial->SetAlbedoColor({ 1.f, 0, 1.f });
	}

	void RendererResources::Shutdown()
	{
		if (!s_ResourcesData)
			return;

		// Release high-level fallbacks before their renderer-owned GPU textures.
		// MaterialAsset itself is renderer-agnostic, while EnvironmentMap borrows the
		// fallback texture handles owned below.
		s_ResourcesData->DefaultMaterial = nullptr;
		s_ResourcesData->EmptyEnvironment = nullptr;

		ResourceManager::DestroyTextureCube(s_ResourcesData->BlackCubeTexture);
		ResourceManager::DestroyTexture2D(s_ResourcesData->BlackTexture);
		ResourceManager::DestroyTexture2D(s_ResourcesData->WhiteTexture);
		ResourceManager::DestroyTexture2D(s_ResourcesData->BrdfLutTexture);

		if (s_ResourcesData->ShaderLibrary)
			s_ResourcesData->ShaderLibrary->ReleaseAllShaders();
		s_ResourcesData->ShaderLibrary = nullptr;

		delete s_ResourcesData;
		s_ResourcesData = nullptr;
	}

	Handle<TextureCube> RendererResources::GetBlackCubeTexture()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->BlackCubeTexture;
	}

	Handle<Texture2D> RendererResources::GetBlackTexture()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->BlackTexture;
	}

	Handle<Texture2D> RendererResources::GetWhiteTexture()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->WhiteTexture;
	}

	Handle<Texture2D> RendererResources::GetBRDFLutTexture()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->BrdfLutTexture;
	}

	Ref<MaterialAsset> RendererResources::GetDefaultPBRMaterial()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->DefaultMaterial;
	}

	Ref<EnvironmentMap> RendererResources::GetEmptyEnvironmentMap()
	{
		CORE_ASSERT(s_ResourcesData, "RendererResources not initialized");
		return s_ResourcesData->EmptyEnvironment;
	}

	ShaderLibrary& RendererResources::GetShaderLibrary()
	{
		CORE_ASSERT(s_ResourcesData && s_ResourcesData->ShaderLibrary, "RendererResources ShaderLibrary not initialized");
		return *s_ResourcesData->ShaderLibrary.get();
	}
}

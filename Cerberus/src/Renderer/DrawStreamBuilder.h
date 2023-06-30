#pragma once
#include "Renderer/Backend/BindGroup.h"
#include "Renderer/Backend/MeshProxy.h"
#include "Renderer/Shader.h"

namespace cerberus
{
	struct DrawArea
	{
		Handle<BindGroup> BindGroup0;
		Handle<BindGroup> BindGroup1;
		uint32_t DrawOffset = 0;
		uint32_t DrawCount = 0;
	};

	struct Draw
	{
		Ref<Shader> Shader;
		Handle
	};

	class DrawStreamBuilder final
	{
	public:


	};
}
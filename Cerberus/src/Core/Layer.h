#pragma once
#include "Core/Timestep.h"
#include "Events/Event.h"
namespace cerberus
{
	class Layer
	{
	public:
		Layer(const std::string name = "Layer")
			: m_LayerName(name) {}
		virtual ~Layer() = default;

		virtual void OnAttach() = 0;
		virtual void OnUpdate(Timestep ts) = 0;
		virtual void OnEvent(Event& event) = 0;
		virtual void OnImGuiRender() = 0;
		// Optional post-present lifecycle boundary. GPU-owning subsystem swaps can
		// safely wait and release resources here without invalidating recorded work.
		virtual void OnFrameEnd() {}
		virtual void OnDetach() = 0;

		inline std::string GetName() { return m_LayerName; }
	private:
		std::string m_LayerName;
	};

}

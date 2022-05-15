#pragma once

namespace cerberus
{
	struct ShadowCascade
	{
		glm::mat4 ViewProjectionMatrix;
		glm::mat4 ViewMatrix;
		float CascadeSplit;
	};

	struct SceneRendererCamera;

	class Shadow
	{
	public:
		Shadow() = default;
		~Shadow();

		void UpdateShadowCascade(const SceneRendererCamera& camera, const glm::mat4& inverseViewProjection, const glm::vec3& lightDirection);

		const ShadowCascade* GetShadowCascades() const { return m_CascadedData; }

		const ShadowCascade& GetShadowCascade(uint32_t index) const 
		{
			CORE_ASSERT(index < 4 && index >= 0, "Index out of bound");
			return m_CascadedData[index];
		}

		float* GetCascadeSplitLambda() { return &m_CascadeSplitLambda; }
		float* GetCascadeFarPlaneOffset() { return &m_CascadeFarPlaneOffset; }
		float* GetCascadeNearPlaneOffset() { return &m_CascadeNearPlaneOffset; }



	private:
		// Four Cascades
		ShadowCascade m_CascadedData [4];
		float m_CascadeSplitLambda = 0.93f;
		float m_CascadeFarPlaneOffset = .0f, m_CascadeNearPlaneOffset = -15.0f;
	};

}
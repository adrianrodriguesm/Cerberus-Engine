#pragma once
namespace cerberus
{

	class Fence
	{
	public:
		Fence(uint32_t count = 0, const std::string& debugName = "");
		~Fence();

		void Release();
		void Wait(uint32_t index = 0, uint64_t timeout = UINT64_MAX);
		void Reset(uint32_t index = 0);

		VkFence GetVulkanFence(uint32_t index = 0)
		{
			CORE_ASSERT(index < m_Fences.size(), "");
			return m_Fences[index];
		}

	private:
		std::vector<VkFence> m_Fences;
		std::string m_DebugName;
	};

}


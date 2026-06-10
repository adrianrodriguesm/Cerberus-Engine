#pragma once
#include <shaderc/shaderc.hpp>
#include <libshaderc_util/file_finder.h>
#include <filesystem>
#include <unordered_set>
namespace cerberus
{
	struct IncludeData
	{
		std::filesystem::path IncludedFilePath{};
		size_t IncludeDepth{};
		bool IsRelative{ false };
		bool IsGuarded{ false };
		uint32_t HashValue{};

		VkShaderStageFlagBits IncludedStage{};

		inline bool operator==(const IncludeData& other) const noexcept
		{
			return this->IncludedFilePath == other.IncludedFilePath && this->HashValue == other.HashValue;
		}
	};

	struct HeaderCache
	{
		std::string Source;
		uint32_t SourceHash;
		VkShaderStageFlagBits Stages;
		bool IsGuarded;
	};

}

namespace std {
	template<>
	struct hash<cerberus::IncludeData>
	{
		size_t operator()(const cerberus::IncludeData& data) const noexcept
		{
			return std::filesystem::hash_value(data.IncludedFilePath) ^ data.HashValue;
		}
	};
}

namespace cerberus
{
	class GLSLIncludeProcessor : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		explicit GLSLIncludeProcessor(const shaderc_util::FileFinder* file_finder);

		~GLSLIncludeProcessor() override;

		shaderc_include_result* GetInclude(const char* requestedPath, shaderc_include_type type, const char* requestingPath, size_t includeDepth) override;

		void ReleaseInclude(shaderc_include_result* data) override;

		std::unordered_set<IncludeData>&& GetIncludeData() { return std::move(m_includeData); }
		std::unordered_set<std::string>&& GetParsedSpecialMacros() { return std::move(m_ParsedSpecialMacros); }

	private:
		// Used by GetInclude() to get the full filepath.
		const shaderc_util::FileFinder& m_FileFinder;
		std::unordered_set<IncludeData> m_includeData;
		std::unordered_set<std::string> m_ParsedSpecialMacros;
		std::unordered_map <std::string, HeaderCache> m_HeaderCache;
	};
}
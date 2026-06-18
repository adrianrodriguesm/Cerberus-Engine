#include "pch.h"
#include "GLSLIncludeProcessor.h"
#include <iosfwd>
#include <regex>
#include <fstream>
#include "Utils/FileSystem.h"
namespace cerberus
{
	int SkipBOM(std::istream& in)
	{
		char test[4] = { 0 };
		in.seekg(0, std::ios::beg);
		in.read(test, 3);
		if (strcmp(test, "\xEF\xBB\xBF") == 0)
		{
			in.seekg(3, std::ios::beg);
			return 3;
		}
		in.seekg(0, std::ios::beg);
		return 0;
	}

	// Returns an empty string when failing.
	std::string ReadFileAndSkipBOM(const std::filesystem::path& filepath)
	{
		std::string result;
		std::ifstream in(filepath, std::ios::in | std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			auto fileSize = in.tellg();
			const int skippedChars = SkipBOM(in);

			fileSize -= skippedChars - 1;
			result.resize(fileSize);
			in.read(result.data() + 1, fileSize);
			// Add a dummy tab to beginning of file.
			result[0] = '\t';
		}
		in.close();
		return result;
	}

	static constexpr uint32_t GenerateFNVHash(const char* str)
	{
		constexpr uint32_t FNV_PRIME = 16777619u;
		constexpr uint32_t OFFSET_BASIS = 2166136261u;

		const size_t length = std::string_view(str).length() + 1;
		uint32_t hash = OFFSET_BASIS;
		for (size_t i = 0; i < length; ++i)
		{
			hash ^= *str++;
			hash *= FNV_PRIME;
		}
		return hash;
	}


	std::vector<std::string> SplitStringAndKeepDelims(std::string str)
	{

		const static std::regex re(R"((^\W|^\w+)|(\w+)|[:()])", std::regex_constants::optimize);

		std::regex_iterator<std::string::iterator> rit(str.begin(), str.end(), re);
		std::regex_iterator<std::string::iterator> rend;
		std::vector<std::string> result;

		while (rit != rend)
		{
			result.emplace_back(rit->str());
			++rit;
		}
		return result;
	}

	bool ContainsHeaderGuard(std::string& header)
	{
		size_t pos = header.find('#');
		while (pos != std::string::npos)
		{
			const size_t endOfLine = header.find_first_of("\r\n", pos) + 1;
			auto tokens = SplitStringAndKeepDelims(header.substr(pos, endOfLine - pos));
			auto it = tokens.begin();

			if (*(++it) == "pragma")
			{
				if (*(++it) == "once")
				{
					header.erase(pos, endOfLine - pos);
					return true;
				}
			}
			pos = header.find('#', pos + 1);
		}
		return false;
	}

	// From https://wandbox.org/permlink/iXC7DWaU8Tk8jrf3 and is modified.
	enum class State : char { SlashOC, StarIC, SingleLineComment, MultiLineComment, NotAComment };

	template <typename InputIt, typename OutputIt>
	void CopyWithoutComments(InputIt first, InputIt last, OutputIt out)
	{
		State state = State::NotAComment;

		while (first != last)
		{
			switch (state)
			{
			case State::SlashOC:
				if (*first == '/') state = State::SingleLineComment;
				else if (*first == '*') state = State::MultiLineComment;
				else
				{
					state = State::NotAComment;
					*out++ = '/';
					*out++ = *first;
				}
				break;
			case State::StarIC:
				if (*first == '/') state = State::NotAComment;
				else state = State::MultiLineComment;
				break;
			case State::NotAComment:
				if (*first == '/') state = State::SlashOC;
				else *out++ = *first;
				break;
			case State::SingleLineComment:
				if (*first == '\n')
				{
					state = State::NotAComment;
					*out++ = '\n';
				}
				break;
			case State::MultiLineComment:
				if (*first == '*') state = State::StarIC;
				else if (*first == '\n') *out++ = '\n';
				break;
			}
			++first;
		}
	}

	inline static VkShaderStageFlagBits StageToVKShaderStage(const std::string_view stage)
	{
		if (stage == "vertex")						return VK_SHADER_STAGE_VERTEX_BIT;
		if (stage == "fragment" || stage == "pixel")	return VK_SHADER_STAGE_FRAGMENT_BIT;
		if (stage == "comp")						return VK_SHADER_STAGE_COMPUTE_BIT;
		if (stage == "rayGen")						return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		if (stage == "closestHit")                   return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		if (stage == "anyHit")						return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		if (stage == "missHit")                      return VK_SHADER_STAGE_MISS_BIT_KHR;
		if (stage == "intersection")                 return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

		return VK_SHADER_STAGE_ALL;
	}

	inline static std::string_view StageToShaderMacro(const std::string_view stage)
	{
		if (stage == "vertex")					return "__VERTEX_STAGE__";
		if (stage == "fragment")				return "__FRAGMENT_STAGE__";
		if (stage == "comp")					return "__COMPUTE_STAGE__";
		if (stage == "rayGen")					return "__RAY_GEN_STAGE__";
		if (stage == "closestHit")               return "__CLOSEST_HIT_STAGE__";
		if (stage == "anyHit")					return "__ANY_HIT_STAGE__";
		if (stage == "missHit")                  return "__MISS_HIT_STAGE__";
		if (stage == "intersection")             return "__INTERSECTION_STAGE__";
		return "";
	}


	VkShaderStageFlagBits PreprocessHeader(std::string& contents, bool& isGuarded, std::unordered_set<std::string>& specialMacros, const std::unordered_set<IncludeData>& includeData, const std::filesystem::path& fullPath)
	{
		std::stringstream sourceStream;
		CopyWithoutComments(contents.begin(), contents.end(), std::ostream_iterator<char>(sourceStream));
		contents = sourceStream.str();

		VkShaderStageFlagBits stagesInHeader = {};

		//Removes header guard in GLSL only.
		isGuarded = ContainsHeaderGuard(contents);

		uint32_t stageCount = 0;
		size_t startOfShaderStage = contents.find('#', 0);

		while (startOfShaderStage != std::string::npos)
		{
			const size_t endOfLine = contents.find_first_of("\r\n", startOfShaderStage) + 1;
			// Parse stage. example: #pragma stage:vert
			auto tokens = SplitStringAndKeepDelims(contents.substr(startOfShaderStage, endOfLine - startOfShaderStage));

			uint32_t index = 0;
			// Pre-processor directives
			if (tokens[index] == "#")
			{
				++index;
				// Pragmas
				if (tokens[index] == "pragma")
				{
					++index;
					// Stages
					if (tokens[index] == "stage")
					{
						CORE_ASSERT(tokens[++index] == ":", "Stage pragma is invalid");

						// Skipped ':'
						const std::string_view stage(tokens[++index]);
						CORE_ASSERT(stage == "vert" || stage == "frag" || stage == "comp", "Invalid shader type specified");
						VkShaderStageFlagBits foundStage = StageToVKShaderStage(stage);


						const bool alreadyIncluded = std::find_if(includeData.begin(), includeData.end(), [fullPath, foundStage](const IncludeData& data)
							{
								return data.IncludedFilePath == fullPath.string() && !bool(foundStage & data.IncludedStage);
							}) != includeData.end();

						if (isGuarded && alreadyIncluded)
							contents.clear();
						else if (!isGuarded && alreadyIncluded)
							NV_CORE_WARN("\"{}\" Header does not contain a header guard (#pragma once).", fullPath);

						// Add #endif
						if (stageCount == 0)
							contents.replace(startOfShaderStage, endOfLine - startOfShaderStage, fmt::format("#ifdef {}\r\n", StageToShaderMacro(stage)));
						else // Add stage macro instead of stage pragma, both #endif and #ifdef must be in the same line, hence no '\n'
							contents.replace(startOfShaderStage, endOfLine - startOfShaderStage, fmt::format("#endif\r\n#ifdef {}", StageToShaderMacro(stage)));

						*(int*)&stagesInHeader |= (int)foundStage;
						stageCount++;
					}
				}
				else if (tokens[index] == "ifdef")
				{
					++index;
					if (tokens[index].rfind("__HZ_", 0) == 0) // Hazel special macros start with "__HZ_"
					{
						specialMacros.emplace(tokens[index]);
					}
				}
				else if (tokens[index] == "if" || tokens[index] == "define") // Consider "#if defined()" too?
				{
					++index;
					for (size_t i = index; i < tokens.size(); ++i)
					{
						if (tokens[i].rfind("__HZ_", 0) == 0) // Hazel special macros start with "__HZ_"
						{
							specialMacros.emplace(tokens[i]);
						}
					}
				}
			}

			startOfShaderStage = contents.find('#', startOfShaderStage + 1);
		}
		if (stageCount)
			contents.append("\n#endif");
		else
		{
			const bool alreadyIncluded = std::find_if(includeData.begin(), includeData.end(), [fullPath](const IncludeData& data)
				{
					return data.IncludedFilePath == fullPath;
				}) != includeData.end();
			if (isGuarded && alreadyIncluded)
				contents.clear();
			else if (!isGuarded && alreadyIncluded)
				NV_CORE_WARN("\"{}\" Header does not contain a header guard (#pragma once)", fullPath);
		}


		return stagesInHeader;
	}


	GLSLIncludeProcessor::GLSLIncludeProcessor(const shaderc_util::FileFinder* file_finder)
		: m_FileFinder(*file_finder)
	{

	}

	GLSLIncludeProcessor::~GLSLIncludeProcessor()
	{

	}

	shaderc_include_result* GLSLIncludeProcessor::GetInclude(const char* requestedPath, shaderc_include_type type, const char* requestingPath, size_t includeDepth)
	{
		std::filesystem::path requestedFullPath;
		if (FileSystem::IsVirtualPath(requestedPath))
		{
			requestedFullPath = FileSystem::ResolvePath(requestedPath);
		}
		else
		{
			requestedFullPath = (type == shaderc_include_type_relative)
				? m_FileFinder.FindRelativeReadableFilepath(requestingPath, requestedPath)
				: m_FileFinder.FindReadableFilepath(requestedPath);
		}

		auto& [source, sourceHash, stages, isGuarded] = m_HeaderCache[requestedFullPath.string()];

		if (source.empty())
		{
			source = ReadFileAndSkipBOM(requestedFullPath);
			if (source.empty())
				CORE_ASSERT(false, "Failed to load included file: {} in {}.", requestedFullPath, requestingPath);
			sourceHash = GenerateFNVHash(source.c_str());

			// Can clear "source" in case it has already been included in this stage and is guarded.
			stages = PreprocessHeader(source, isGuarded, m_ParsedSpecialMacros, m_includeData, requestedFullPath);
		}
		else if (isGuarded)
		{
			source.clear();
		}

		// Does not emplace if it finds the same include path and same header hash value.
		m_includeData.emplace(IncludeData{ requestedFullPath, includeDepth, type == shaderc_include_type_relative, isGuarded, sourceHash, stages });

		auto* const container = new std::array<std::string, 2>;
		(*container)[0] = requestedPath;
		(*container)[1] = source;
		auto* const data = new shaderc_include_result;

		data->user_data = container;

		data->source_name = (*container)[0].data();
		data->source_name_length = (*container)[0].size();

		data->content = (*container)[1].data();
		data->content_length = (*container)[1].size();

		return data;
	}

	void GLSLIncludeProcessor::ReleaseInclude(shaderc_include_result* data)
	{
		delete static_cast<std::array<std::string, 2>*>(data->user_data);
		delete data;
	}

}
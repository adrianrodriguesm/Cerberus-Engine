#pragma once

namespace cerberus::utils
{
	std::string BytesToString(uint64_t bytes);
	std::string GetFilename(const std::string& filepath);
	std::string GetExtension(const std::string& filename);
	std::string RemoveExtension(const std::string& filename);
	bool StartsWith(const std::string& string, const std::string& start);
	std::vector<std::string> SplitString(const std::string& string, const std::string& delimiters);
	std::vector<std::string> SplitString(const std::string& string, const char delimiter);
	std::string ToLower(const std::string& string);
	std::string ToUpper(const std::string& string);
}
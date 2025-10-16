#pragma once
class Helpful
{
public:
	static bool MatchPattern(size_t addr, const std::vector<uint8_t>& pattern);
	static size_t FindPattern(const std::vector<uint8_t>& pattern, int idx);
	static std::vector<size_t> FindAllPatterns(const std::vector<uint8_t>& pattern);
	static bool KeyPressedOnce(int vk);
};


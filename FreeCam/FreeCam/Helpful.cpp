#include "pch.h"
#include "Helpful.h"
#include "CDK.h"

using namespace PlayStation2;


bool Helpful::MatchPattern(size_t addr, const std::vector<uint8_t>& pattern) {
    for (size_t i = 0; i < pattern.size(); i++) {
        try {
            if (PS2Memory::ReadEE<uint8_t>(addr + i) != pattern[i])
                return false;
        }
        catch (...) {
            return false; // out-of-bounds
        }
    }
    return true;
}

size_t Helpful::FindPattern(const std::vector<uint8_t>& pattern, int idx) {
    // Make a reversed copy
    std::vector<uint8_t> pat(pattern.rbegin(), pattern.rend());

    constexpr size_t EE_MEM_SIZE = 0x2000000; // 32 MB
    for (size_t k = 0; k <= EE_MEM_SIZE - pat.size(); k++) {
        if (MatchPattern(k, pat) && idx == 1)
            return k;
        else if (MatchPattern(k, pat))
            idx--;
    }
    return 0;
}
std::vector<size_t> Helpful:: FindAllPatterns(const std::vector<uint8_t>& pattern) {
    std::vector<size_t> matches;
    if (pattern.empty()) return matches;

    // Reverse pattern if your MatchPattern expects reversed (remove if not)
    std::vector<uint8_t> pat(pattern.rbegin(), pattern.rend());

    constexpr size_t EE_MEM_SIZE = 0x2000000; // 32 MB
    const size_t patSize = pat.size();

    for (size_t addr = 0; addr <= EE_MEM_SIZE - patSize; addr++) {
        bool match = true;

        for (size_t i = 0; i < patSize; i++) {
            uint8_t byte = 0;
            try {
                byte = PS2Memory::ReadEE<uint8_t>(addr + i);
            }
            catch (...) {
                match = false;
                break;
            }

            if (byte != pat[i]) {
                match = false;
                break;
            }
        }

        if (match) matches.push_back(addr);
    }

    return matches;
}

bool Helpful::KeyPressedOnce(int vk) {
    static SHORT lastState[256] = {};
    SHORT current = GetAsyncKeyState(vk);
    bool pressed = (current & 0x1) && !(lastState[vk] & 0x1);
    lastState[vk] = current;
    return pressed;
}

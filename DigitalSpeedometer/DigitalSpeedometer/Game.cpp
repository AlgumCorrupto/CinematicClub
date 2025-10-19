#include "pch.h"
#include "Game.h"
#include "CDK.h"
#include <vector>

using namespace PlayStation2;

Game::Conversion Game::speedConversion = Game::Conversion::mph;
const unsigned int Game::formatted_string_addr = 0x0066FEA2; // address where we will write the formatted speed string
unsigned int Game::playerBase = 0;
unsigned int Game::velocityBase = 0;
bool Game::active = true;

bool MatchPattern(size_t addr, const std::vector<uint8_t>& pattern) {
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

size_t FindPattern(const std::vector<uint8_t>& pattern, int idx) {
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
std::vector<size_t> FindAllPatterns(const std::vector<uint8_t>& pattern) {
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

namespace {
    void WriteStringToEE(size_t addr, const char* str) {
        while (*str) {
            PS2Memory::WriteEE<uint8_t>(addr++, static_cast<uint8_t>(*str++));
        }
        PS2Memory::WriteEE<uint8_t>(addr, 0); // Null-terminate
    }
}

void Game::Init() {
    WriteStringToEE(formatted_string_addr, "Digital Speedometer\nBy AlgumCorrupto"); // write string to EE memory
    PS2Memory::WriteEE<unsigned int>(0x006F60B0, true); // enable debug display
    Sleep(2000);
	Game::CheckWhereVelocityIsStored();
    PS2Memory::WriteEE<unsigned int>(0x0052DA40, 0x240500FF); // put the debug display slighty down
    PCSX2::recResetEE_stub();
}

void Game::CheckWhereVelocityIsStored() {
    auto candidates = FindAllPatterns({ 0x00, 0x62, 0x77, 0x60 });
    for (auto addr : candidates) {
        if (PS2Memory::ReadEE<unsigned int>(addr + 0x40) == 0x00628210) {
			playerBase = addr;
            break;
        }
        playerBase = 0;
	}
    if (playerBase == 0) {
		velocityBase = 0;
        return;
    } else {
        velocityBase = PS2Memory::ReadEE<unsigned int>(playerBase - 56);
        return;
    }
}

static bool KeyPressedOnce(int vk) {
    static SHORT lastState[256] = {};
    SHORT current = GetAsyncKeyState(vk);
    bool pressed = (current & 0x1) && !(lastState[vk] & 0x1);
    lastState[vk] = current;
    return pressed;
}

void Game::Loop() {
	if (PS2Memory::ReadEE<unsigned int>(0x6144BC) != 0x00000000) return; // if game is loading, skip

    if (velocityBase == 0 || PS2Memory::ReadEE<unsigned int>(velocityBase) != 0x6278F8) return;

    // Toggle speedConversion when ',' is pressed (once per press)
    if (KeyPressedOnce('M')) {
        speedConversion = (speedConversion == Conversion::mph) ? Conversion::kph : ((speedConversion == Conversion::kph) ? Conversion::ms : Conversion::mph);
    }
    if(KeyPressedOnce('N')) {
        active = !active;
        PS2Memory::WriteEE<unsigned int>(0x006F60B0, active); // enable debug display
	}
	if (KeyPressedOnce('B')) CheckWhereVelocityIsStored(); // manual re-check
    float velocity = PS2Memory::ReadEE<float>(velocityBase + 0xB0);
    //float velocity = PS2Memory::ReadEE<float>(velocityBase + 0x80);
    //float velocity = PS2Memory::ReadEE<float>(velocityBase + 0x148);
    char buffer[32] = {0}; // Allocate buffer with fixed size

	velocity = fabs(velocity); // absolute value
    switch (speedConversion)
    {
    case Game::Conversion::mph:
        velocity *= 2.23694f;
        snprintf(buffer, sizeof(buffer), "%3.0f mph", velocity); // safer alternative
        break;
    case Game::Conversion::kph:
        velocity *= 3.6f;
        snprintf(buffer, sizeof(buffer), "%3.0f kmh", velocity);
        break;
    case Game::Conversion::ms:
        snprintf(buffer, sizeof(buffer), "%3.0f m/s", velocity);
        break;
    default:
        break;
    }

    WriteStringToEE(formatted_string_addr, buffer);
}

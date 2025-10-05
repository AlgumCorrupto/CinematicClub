#include "pch.h"
#include "Game.h"
#include "CDK.h"
#include "Playa.h"
#include "mat3x4f.h"
#include "types.h"

using namespace PlayStation2;

using namespace sr2;
using namespace sr2::math;

bool Game::frozen = false;
unsigned int Game::car_base = NULL;
unsigned int Game::routine_address = NULL;
unsigned char Game::car_to_select = 1;

void Game::Init() {
    Game::car_base = NULL;
    Game::Detect();
}

void Game::Increment_car_to_select() {
    if (car_to_select < 255)
        car_to_select++;
    else
		car_to_select = 255;
    Game::GetCarBase();

    printf("Current selected opponent index: %d\n", car_to_select);
}

void Game::Decrement_car_to_select() {
    if (car_to_select > 0)
        car_to_select--;
    else
		car_to_select = 0;
    Game::GetCarBase();
    printf("Current selected opponent index: %d\n", car_to_select);
}



void Game::Loop() {
    Sleep(33);

    // Toggle camera freeze/unfreeze
    if (GetAsyncKeyState(VK_NUMPAD1) & 1) Game::CarFreeze();
    if (GetAsyncKeyState(VK_NUMPAD2) & 1) Game::CameraUnfreeze();

    // quick and dirty fix if the car to transform is the opponent
    if (GetAsyncKeyState('O') & 1) Game::Decrement_car_to_select();
    if (GetAsyncKeyState('P') & 1) Game::Increment_car_to_select();

    if (frozen) {
        Playa::Loop();

        if (!car_base) return; // sanity check

        if(true)
        for (int row = 0; row < 4; row++) {        // rotation rows
            for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
                PS2Memory::WriteEE<float>(
                    car_base + 0x10 + (row * 3 + col) * sizeof(float),
                    Playa::output[row][col]
                );
            }
         }
    }
}


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


void Game::GetCarBase() {
    //std::vector<uint8_t> cam_pattern = { 0x18, 0x04, 0x63, 0x00 };
    //Game::car_base = FindPattern({ 0x00, 0x62, 0x2D, 0xF8 }, 1);
    Game::car_base = FindPattern({ 0x00, 0x62, 0x76, 0x30 }, car_to_select + 1);
    if (car_base == 0) {
        car_to_select = 0;
    }
}

void Game::CameraUnfreeze() {
    PCSX2::recResetEE_stub();

    printf("Unfrozen camera\n");
    auto mem = CGlobals::g_memory;
  

    PCSX2::recResetEE_stub();

    //mem->BytePatch(instruction_address, (unsigned char*)0x0C163760, 4);
    frozen = false;
}

void Game::CarFreeze() {
    printf("Frozen camera\n");
    auto mem = CGlobals::g_memory;

    PCSX2::recResetEE_stub();

    Game::frozen = true;
    Game::GetCarBase();
	Playa::moveSpeed = 0.5f;

    Playa::Init(Game::car_base);
}

void Game::Detect() {
    Game::routine_address = FindPattern({ 0x8D, 0x69, 0x00, 0x0C }, 1);
    //Game::car_base = FindPattern({ 0x00, 0x62, 0x2D, 0xF8 }, 1);
    GetCarBase();

    printf("routine: %X, car: %X\n", routine_address, car_base);
}
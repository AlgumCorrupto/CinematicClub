#include "pch.h"
#include "Game.h"
#include "CDK.h"
#include "Playa.h"
#include "mat3x4f.h"
#include "types.h"
#include <vector>
//lui  $v0, 0x001A # load upper 16 bits->$v0 = 0x001A0150
//ori  $v0, $v0, 0x0150  # set lower 16 bits->$v0 = 0x001A0150
//jr   $ra
//nop


using namespace PlayStation2;

using namespace sr2;
using namespace sr2::math;

bool Game::frozen = false;
unsigned int Game::cam_base = NULL;
std::vector<float> Game::fov_bases = {};
bool Game::cinematicMode = false;
bool Game::mouseLocked = false;
HWND Game::gameWindow;
POINT Game::lockCenter;

#ifdef DUB_EDITION
//const unsigned int Game::routine_address = 0x511da0;
//const unsigned int Game::unexplored_memory = 0x001A0x0150;
//const unsigned int Game::fovInstruction = 0x0051C9A0;
#elif defined DUB_EDITION_REMIX
const unsigned int Game::camRoutine = 0x515790;
const unsigned int Game::unusedMemory1 = 0x001A0150; // used to store our camera matrix
const unsigned int Game::unusedMemory2 = 0x001A001C; // used to store custom instructions
const unsigned int Game::fovInstruction = 0x0031F2A4;
#endif
//0x001A0150 + 0x10 * 0x4

void Game::Init() {
	gameWindow = FindWindowA(NULL, "Midnight Club 3 - DUB Edition Remix");
    Game::cam_base = NULL;

    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 0 * sizeof(unsigned int), 0x3C08001A);
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 1 * sizeof(unsigned int), 0x35080190);
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 2 * sizeof(unsigned int), 0x8D090000); //  lw $t1, 0($t0)    # Load word from address in $t0 to $at
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 3 * sizeof(unsigned int), 0x44896000); // mtc1 $t1, $f12    # Move from $at to $f12
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 4 * sizeof(unsigned int), 0x92020048);
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 5 * sizeof(unsigned int), 0x03E00008);
    PS2Memory::WriteEE<unsigned int>(unusedMemory2 + 6 * sizeof(unsigned int), 0x00000000);
    PCSX2::recResetEE_stub();

}

static bool KeyPressedOnce(int vk) {
    static SHORT lastState[256] = {};
    SHORT current = GetAsyncKeyState(vk);
    bool pressed = (current & 0x1) && !(lastState[vk] & 0x1);
    lastState[vk] = current;
    return pressed;
}


void Game::Loop() {

    // --- Toggle camera freeze/unfreeze ---
    if (KeyPressedOnce('P')) (!frozen) ? Game::CameraFreeze() : Game::CameraUnfreeze();

    // --- Toggle mouse lock ---
    if (KeyPressedOnce('L')) mouseLocked = !mouseLocked;

    // --- Toggle cinematic mode ---
    if (KeyPressedOnce(VK_MENU)) cinematicMode = !cinematicMode;

    if (frozen) {
        Playa::Loop();

        if (!cam_base) return; // sanity check

        for (int row = 0; row < 4; row++) {        // rotation rows
            for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
                PS2Memory::WriteEE<float>(
                    unusedMemory1 + (row * 3 + col) * sizeof(float),
                    Playa::output[row][col]
                );
            }
         }
        PS2Memory::WriteEE(unusedMemory1 + 16 * 4, Playa::fov);
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


void Game::GetCamBase() {
#ifdef DUB_EDITION
    Game::cam_base = FindPattern({ 0x00, 0x63, 0x04, 0x18 }, 2) + 0x010;
#elif defined DUB_EDITION_REMIX
    std::vector<size_t> candidates;
    candidates = FindAllPatterns({ 0x00, 0x00, 0x6B, 0x60 }); // +0x1EC;
    for (auto addr : candidates) {
        if(PS2Memory::ReadEE<unsigned int>(addr + (0x10 - 0x4)) == 0xCDCDCDCD) {
            Game::cam_base = addr + 0x1EC;
            break;
		}
    }
    // find all fov locations
    fov_bases.clear();
    auto matches = FindAllPatterns({ 0x00, 0x63, 0x4C, 0xB0 });
    printf("Found %zu matches:\n", matches.size());
    for (auto addr : matches) {
        printf("  0x%08zX\n", addr);
    }

    // Example: Use them to populate fov_bases
    for (auto addr : matches) {
        uint32_t check = 0;
        try {
            check = PS2Memory::ReadEE<uint32_t>(addr + 0x10);
        }
        catch (...) {
            continue;
        }

        if (check != 0x3F800000)
            fov_bases.push_back(addr + 0x88);
    }
#endif
}

void Game::CameraUnfreeze() {
    printf("Unfrozen camera\n");
    auto mem = CGlobals::g_memory;

    PS2Memory::WriteEE<unsigned int>(camRoutine + 1 * sizeof(unsigned int), 0x8C430030);
    PS2Memory::WriteEE<unsigned int>(camRoutine + 2 * sizeof(unsigned int), 0x9064007C);
    PS2Memory::WriteEE<unsigned int>(camRoutine + 3 * sizeof(unsigned int), 0x24620040);
    PS2Memory::WriteEE<unsigned int>(camRoutine + 4 * sizeof(unsigned int), 0x24630010);
    PS2Memory::WriteEE(fovInstruction, 0xC46C0088);

    PCSX2::recResetEE_stub();
	UnlockAndShowMouse();
    frozen = false;
}

void Game::CameraFreeze() {
    printf("Frozen camera\n");
    Game::GetCamBase();
    Playa::Init(Game::cam_base);

    auto mem = CGlobals::g_memory;
	// reroute camera matrix pointer to our own matrix
#ifdef DUB_EDITION
    PS2Memory::WriteEE<unsigned int>(routine_address + 1 * sizeof(unsigned int), 0x3C020068);
    PS2Memory::WriteEE<unsigned int>(routine_address + 2 * sizeof(unsigned int), 0x3442B1A0);
#elif defined DUB_EDITION_REMIX
    PS2Memory::WriteEE<unsigned int>(camRoutine + 1 * sizeof(unsigned int), 0x3C02001A);
    PS2Memory::WriteEE<unsigned int>(camRoutine + 2 * sizeof(unsigned int), 0x34420150);
    PS2Memory::WriteEE<unsigned int>(fovInstruction, 0xc068007);
#endif
    PS2Memory::WriteEE<unsigned int>(camRoutine + 3 * sizeof(unsigned int), 0x03e00008);
    PS2Memory::WriteEE<unsigned int>(camRoutine + 4 * sizeof(unsigned int), 0x00000000);

    PCSX2::recResetEE_stub();

    Game::frozen = true;
	Game::mouseLocked = true;
	Playa::moveSpeed = 0.5f;
}

void Game::Detect() {
    GetCamBase();
}

void Game::LockAndHideMouse(HWND hwnd) {
    // Get up-to-date window center in screen coordinates
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT center = {
        (rect.right - rect.left) / 2,
        (rect.bottom - rect.top) / 2
    };
    ClientToScreen(hwnd, &center);
    lockCenter = center;

    // Move cursor to the center BEFORE clipping
    SetCursorPos(center.x, center.y);

    // Now lock cursor to a 1×1 rectangle at the center
    RECT clipRect = { center.x, center.y, center.x + 1, center.y + 1 };
    ClipCursor(&clipRect);
}


void Game::UnlockAndShowMouse() {
    // Release cursor and show it again
    ClipCursor(nullptr);
    ShowCursor(TRUE);
}
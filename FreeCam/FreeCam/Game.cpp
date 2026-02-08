#include "pch.h"
#include "Game.h"
#include "CDK.h"
#include "Playa.h"
#include "mat3x4f.h"
#include "types.h"
#include <vector>
#include "OpponentCam.h"
#include "Helpful.h"
#include <chrono>
#include <thread>
#include "ParentedCam.h"

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
mat3x4f Game::cameraTransform;
Game::Mode Game::mode = Game::Mode::unfrozen;
float Game::fov = 40.f;
float Game::mouseSensitivity = 3.3;
float Game::toSleep = 33.f;
using clockq = std::chrono::steady_clock;
static auto lastTime = clockq::now();
float Game::deltaTime = 0;

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

void Game::NextState() {
    // unfrozen -> opponent -> free -> unfrozen
    switch (mode) {
    case Mode::unfrozen:
        Game::GetCamBase();
        if (OrbitCam::Init(Game::cam_base) == false) {
            mode = Mode::parent;
            goto next;
        }
        toSleep = 0.f;
        CameraFreeze();
        mode = Mode::opponent;
        break;
    case Mode::opponent:
        Game::GetCamBase();
        ParentedCam::Init(Game::cam_base);
        toSleep = 0.f;
        CameraFreeze();
        mode = Mode::parent;
        break;
    case Mode::parent:
        Game::GetCamBase();
        CameraFreeze(); // <-- Freeze BEFORE reading cam_base
        FreeCam::Init(Game::cam_base); // <-- Now cam_base has the correct live transform
        mode = Mode::free;
        toSleep = 0.f;
        break;
    case Mode::free:
        CameraUnfreeze();
        mode = Mode::unfrozen;
    }
    return;
next:
    NextState();
}

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
void IncrementMouseSpeed() {
    float factor = 1.0f + 2.5f * Game::deltaTime;
    Game::mouseSensitivity *= factor;

    // Optional: prevent getting stuck at zero
    if (Game::mouseSensitivity < 0.01f)
        Game::mouseSensitivity = 0.01f;
}

void DecrementMouseSpeed() {
    float factor = 1.0f - 2.5f * Game::deltaTime;
    Game::mouseSensitivity *= factor;

    if (Game::mouseSensitivity < 0.0f)
        Game::mouseSensitivity = 0.0f;
}

void Game::Loop() {

    // --- Toggle camera freeze/unfreeze ---
    if (Helpful::KeyPressedOnce('P')) NextState();

    // --- Toggle mouse lock ---
    if (Helpful::KeyPressedOnce('L')) {
        mouseLocked = !mouseLocked;
        if (mouseLocked) {
            // --- Get pwindow center ---
            POINT center;
            {
                RECT rect;
                GetClientRect(Game::gameWindow, &rect);
                center.x = (rect.right - rect.left) / 2;
                center.y = (rect.bottom - rect.top) / 2;
                ClientToScreen(Game::gameWindow, &center);
            }

            // --- Read mouse position ---
            POINT mousePos;
            SetCursorPos(center.x, center.y);
        }
    }

    // --- Toggle cinematic mode ---
    if (Helpful::KeyPressedOnce(VK_TAB)) cinematicMode = !cinematicMode;

    auto now = clockq::now();
    std::chrono::duration<float> delta = now - lastTime;
    lastTime = now;
    Game::deltaTime = delta.count(); // seconds since last frame
    if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) DecrementMouseSpeed();
    if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) IncrementMouseSpeed();

    if (frozen) {
        if (!cam_base) return; // sanity check
        switch (mode) {
        case Mode::free:
            FreeCam::Loop();
            cameraTransform = FreeCam::transform;
            fov = FreeCam::fov;
            break;
        case Mode::opponent:
            OrbitCam::Loop();
            cameraTransform = OrbitCam::transform;
            fov = OrbitCam::fov;
            break;
        case Mode::parent:
            ParentedCam::Loop();
            cameraTransform = ParentedCam::transform;
            fov = ParentedCam::fov;
            break;
        }
        for (int row = 0; row < 4; row++) {        // rotation rows
            for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
                PS2Memory::WriteEE<float>(
                    unusedMemory1 + (row * 3 + col) * sizeof(float),
                    cameraTransform[row][col]
                );
            }
        }
        PS2Memory::WriteEE(unusedMemory1 + 16 * 4, fov);
    }
}


void Game::GetCamBase() {
#ifdef DUB_EDITION
    Game::cam_base = FindPattern({ 0x00, 0x63, 0x04, 0x18 }, 2) + 0x010;
#elif defined DUB_EDITION_REMIX
    std::vector<size_t> candidates;
    candidates = Helpful::FindAllPatterns({ 0x00, 0x00, 0x6B, 0x60 }); // +0x1EC;
    for (auto addr : candidates) {
        if(PS2Memory::ReadEE<unsigned int>(addr + (0x10 - 0x4)) == 0xCDCDCDCD) {
            Game::cam_base = addr + 0x1EC;
            break;
		}
    }
    // find all fov locations
    fov_bases.clear();
    auto matches = Helpful::FindAllPatterns({ 0x00, 0x63, 0x4C, 0xB0 });

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

    // --- Get window center ---
    POINT center;
    {
        RECT rect;
        GetClientRect(Game::gameWindow, &rect);
        center.x = (rect.right - rect.left) / 2;
        center.y = (rect.bottom - rect.top) / 2;
        ClientToScreen(Game::gameWindow, &center);
    }

    // --- Read mouse position ---
    POINT mousePos;
    SetCursorPos(center.x, center.y);
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
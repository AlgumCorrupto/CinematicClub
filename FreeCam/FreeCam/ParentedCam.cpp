#include "pch.h"
#include "ParentedCam.h"
#include "OpponentCam.h"

#include <cmath>
#include <Windows.h>
#include "types.h"
#include "mat3x4f.h"
#include "mathfunc.h"
#include <thread>
#include <chrono>

#include "CDK.h"

#include "Playa.h"
#include "Game.h"
#include "Helpful.h"

using namespace PlayStation2;
using namespace sr2;
using namespace sr2::math;

// Statics
mat3x4f ParentedCam::transform{};
mat3x4f ParentedCam::offset{};
mat3x4f ParentedCam::targetTransform{};

float ParentedCam::moveSpeed = 2.f; // units per frame

float ParentedCam::yaw = 0.0f;
float ParentedCam::pitch = 0.0f;
float ParentedCam::tilt = 0.0f;

// --- Mouse look (FPS style with cinematic smoothing) ---
float ParentedCam::smoothed_dx = 0.0f;
float ParentedCam::smoothed_dy = 0.0f;
const float ParentedCam::smoothing = 0.1f;  // smaller = smoother motion

// --- Smooth movement variables ---
vec3f ParentedCam::velocity(0.0f, 0.0f, 0.0f);
const float ParentedCam::accel = 1.f;      // acceleration factor
const float ParentedCam::friction = 1.0f;  // friction factor
std::vector<size_t> ParentedCam::opponents = {};
unsigned char ParentedCam::currentOpponent = 0;
bool ParentedCam::rotateWithVehicle = true;
float ParentedCam::mouseElapsed = 0.f;

float ParentedCam::fov = 40.f; // degrees
static bool firstFrame = true;

void ParentedCam::Init(int cam_address)
{
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            transform[row][col] = PS2Memory::ReadEE<float>(
                cam_address + (row * 3 + col) * sizeof(float)
            );
        }


    }
    opponents = {};

    opponents = Helpful::FindAllPatterns({ 0x00, 0x62, 0x76, 0x30 });
    currentOpponent = OrbitCam::currentOpponent;
    if (opponents.size() < currentOpponent) currentOpponent = 0;
    printf("Found %d opponents\n", (int)opponents.size());
	ParentedCam::SetOpponent(currentOpponent);
    firstFrame = true;
}

void MakeLocalLookAt(mat3x4f& offset, const vec3f& forward)
{
    vec3f f = forward.normalized();

    // Choose world up unless forward is vertical
    vec3f worldUp = { 0.f, 1.f, 0.f };

    // Right = up × forward
    vec3f r = worldUp.cross(f).normalized();

    // If forward is too close to worldUp, fix degeneracy
    if (r.length() < 0.0001f)
    {
        worldUp = { 1.f, 0.f, 0.f };
        r = worldUp.cross(f).normalized();
    }

    // Up = forward × right
    vec3f u = f.cross(r).normalized();

    // Write into offset (as a 3x4 matrix)
    offset[0] = r;    // right
    offset[1] = u;    // up
    offset[2] = f;    // forward
    // keep offset[3] unchanged (this is your translation)
}

void MakeLookAtYawPitch(const vec3f& forward, float& yaw, float& pitch)
{
    vec3f f = forward.normalized();
    yaw = atan2f(f.x, f.z);
    pitch = -asinf(f.y);
}


void ParentedCam::SetOpponent(unsigned char index)
{
    if (opponents.empty()) return;

    currentOpponent = index;
    size_t opponentAddr = opponents[currentOpponent];

    // Read target transform from PS2 memory
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            targetTransform[row][col] = PS2Memory::ReadEE<float>(
                opponentAddr + 0x10 + (row * 3 + col) * sizeof(float)
            );
        }
    }

    // Reset camera input/motion
    smoothed_dx = 0.f;
    smoothed_dy = 0.f;
    tilt = 0.f;
    velocity.zero();

    // Reset matrices
    transform = mat3x4f::identity;
    offset = mat3x4f::identity;

    // Default offset behind/above vehicle
    offset[3] = { 0.f, 2.f, 6.f };

    // Reset relative yaw/pitch to zero (so camera is aligned with vehicle)
    yaw = 0.f;
    pitch = 0.f;

    printf("Switched to opponent %u\n", index);
}


void ParentedCam::Loop()
{
    mouseElapsed += Game::deltaTime;

    if (opponents.empty()) return;
    if (currentOpponent >= opponents.size()) SetOpponent(0);

    size_t opponentAddr = opponents[currentOpponent];
    if (PS2Memory::ReadEE<unsigned int>(opponentAddr) != 0x00627630)
        return;

    // --- 1. Read vehicle transform ---
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            targetTransform[r][c] =
            PS2Memory::ReadEE<float>(opponentAddr + 0x10 + (r * 3 + c) * 4);

    // Vehicle basis
    vec3f vRight = targetTransform[0];
    vec3f vUp = targetTransform[1];
    vec3f vFwd = targetTransform[2];
    vec3f vPos = targetTransform[3];

    // --- 2. Handle input ---
    float boost = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 5.0f : 1.0f;
    float mv = moveSpeed * boost * Game::deltaTime;

    if (Helpful::KeyPressedOnce('T'))
        SetOpponent((currentOpponent == 0 ? opponents.size() - 1 : currentOpponent - 1));
    if (Helpful::KeyPressedOnce('Y'))
        SetOpponent((currentOpponent + 1) % opponents.size());
    if (Helpful::KeyPressedOnce('G'))
        rotateWithVehicle = !rotateWithVehicle;

    if (GetAsyncKeyState('Z') & 0x8000) fov += 25.f * Game::deltaTime;
    if (GetAsyncKeyState('X') & 0x8000) fov -= 25.f * Game::deltaTime;
    if (GetAsyncKeyState('N') & 0x8000) DecrementMoveSpeed();
    if (GetAsyncKeyState('M') & 0x8000) IncrementMoveSpeed();


    if (GetAsyncKeyState('O') & 0x8000) tilt = 0;
    if (GetAsyncKeyState('Q') & 0x8000) tilt += 0.5f * Game::deltaTime;
    if (GetAsyncKeyState('E') & 0x8000) tilt -= 0.5f * Game::deltaTime;

    // --- Mouse look ---
    if (Game::mouseLocked && !firstFrame)
    {
        RECT crect;
        GetClientRect(Game::gameWindow, &crect);

        POINT center{
            crect.right / 2,
            crect.bottom / 2
        };
        ClientToScreen(Game::gameWindow, &center);

        POINT mp;
        GetCursorPos(&mp);

        float dx = float(mp.x - center.x);
        float dy = float(mp.y - center.y);

        // Clamp insane spikes (VERY IMPORTANT)
        dx = std::clamp(dx, -100.0f, 100.0f);
        dy = std::clamp(dy, -100.0f, 100.0f);
        float dt = min(Game::deltaTime, .0015f);

        if (Game::cinematicMode) {
            smoothed_dx = expDecay(smoothed_dx, dx, 2.5f, dt);
            smoothed_dy = expDecay(smoothed_dy, dy, 2.5f, dt);
        }
        else {
            smoothed_dx = dx;
            smoothed_dy = dy;
        }

        yaw -= smoothed_dx * Game::mouseSensitivity * dt;
        pitch -= smoothed_dy * Game::mouseSensitivity * dt;

        SetCursorPos(center.x, center.y);
    }



    // --- 4. Build clean camera basis once ---
    vec3f camForward = {
        cosf(pitch) * sinf(yaw),
        -sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
    camForward.normalize();

    vec3f camRight = { cosf(yaw), 0, -sinf(yaw) };
    camRight.normalize();

    vec3f camUp = camForward.cross(camRight).normalized();

    // --- Apply roll ---
    if (tilt != 0.f) {
        float c = cosf(tilt), s = sinf(tilt);
        vec3f r = camRight * c + camUp * s;
        vec3f u = camUp * c - camRight * s;
        camRight = r;
        camUp = u;
    }

    vec3f moveDir(0, 0, 0);

    if (GetAsyncKeyState('W') & 0x8000) moveDir -= camForward;
    if (GetAsyncKeyState('S') & 0x8000) moveDir += camForward;
    if (GetAsyncKeyState('A') & 0x8000) moveDir -= camRight;
    if (GetAsyncKeyState('D') & 0x8000) moveDir += camRight;
    if (GetAsyncKeyState('R') & 0x8000) moveDir += camUp;
    if (GetAsyncKeyState('F') & 0x8000) moveDir -= camUp;

    if (moveDir.lengthSq() > 0.0f)
        moveDir.normalize();

    velocity = moveDir * mv;
    offset[3] += velocity;

    // --- 6. Build final camera matrix ---
    if (rotateWithVehicle)
    {
        // rotate offset into vehicle space
        vec3f off =
            vRight * offset[3].x +
            vUp * offset[3].y +
            vFwd * offset[3].z;

        transform[0] = vRight * camRight.x + vUp * camRight.y + vFwd * camRight.z;
        transform[1] = vRight * camUp.x + vUp * camUp.y + vFwd * camUp.z;
        transform[2] = vRight * camForward.x + vUp * camForward.y + vFwd * camForward.z;
        transform[3] = vPos + off;
    }
    else
    {
        transform[0] = camRight;
        transform[1] = camUp;
        transform[2] = camForward;
        transform[3] = vPos + offset[3];
    }
    firstFrame = false;
}


void ParentedCam::IncrementMoveSpeed() {
    float factor = 1.0f + 2.5f * Game::deltaTime;
    moveSpeed *= factor;

    // Optional: prevent getting stuck at zero
    if (moveSpeed < 0.01f)
        moveSpeed = 0.01f;
}

void ParentedCam::DecrementMoveSpeed() {
    float factor = 1.0f - 2.5f * Game::deltaTime;
    moveSpeed *= factor;

    if (moveSpeed < 0.0f)
        moveSpeed = 0.0f;
}

void ParentedCam::MoveCurrentVehToCamera() {
    auto routine_address = Helpful::FindPattern({ 0x8D, 0x69, 0x00, 0x0C }, 1);
#pragma region freeze car
    // 004D76F4
    // 28
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x28, 0x00000000);
    // 3C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x3C, 0x00000000);
    // 44
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x44, 0x00000000);
    // 4C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x4C, 0x00000000);
    // 54
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x54, 0x00000000);
    // 5C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x5C, 0x00000000);
    // 64
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x64, 0x00000000);
    // 6C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x6C, 0x00000000);
    // 74
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x74, 0x00000000);
    // 7C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x7C, 0x00000000);
    // 84
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x84, 0x00000000);
    // 8C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x8C, 0x00000000);
    // DC E7A40000
    PS2Memory::WriteEE<unsigned int>(routine_address + 0xDC, 0x00000000);
    // 110 004D7804
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x110, 0x00000000);
    // 140 E7A40000
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x140, 0x00000000);
    // 148 E7A20004
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x148, 0x00000000);
    // 14C E7A40010
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x14C, 0x00000000);
    // 158 E7A10018
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x158, 0x00000000);
    // 15C E7A10008
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x15C, 0x00000000);
    // 168
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x168, 0x00000000);
    // 17C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x17C, 0x00000000);
    // 188
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x188, 0x00000000);


    PCSX2::recResetEE_stub();
#pragma endregion

    Sleep(100);
    for (int row = 0; row < 4; row++) {        // rotation rows
        for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
            PS2Memory::WriteEE<float>(
                opponents[currentOpponent] + 0x10 + (row * 3 + col) * sizeof(float),
                transform[row][col]
            );
        }
    }
    printf("current opponent : 0x%X moved to camera position\n", opponents[currentOpponent]);
    Sleep(100);

#pragma region unfreeze car
    // 004D76F4
// 28
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x28, 0xE5000000);
    // 3C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x3C, 0xE5000004);
    // 44
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x44, 0xE5010008);
    // 4C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x4C, 0xE500000C);
    // 54
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x54, 0xE4610004);
    // 5C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x5C, 0xE4600008);
    // 64
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x64, 0xE5010018);
    // 6C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x6C, 0xE4800004);
    // 74
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x74, 0xE4810008);
    // 7C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x7C, 0xE5000024);
    // 84
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x84, 0xE4E10004);
    // 8C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x8C, 0xE4E00008);
    // DC E7A40000
    PS2Memory::WriteEE<unsigned int>(routine_address + 0xDC, 0xE7A40000);
    // 110 004D7804
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x110, 0x004D7804);
    // 140 E7A40000
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x140, 0xE7A40000);
    // 148 E7A20004
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x148, 0xE7A20004);
    // 14C E7A40010
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x14C, 0xE7A40010);
    // 158 E7A10018
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x158, 0x00000000);
    // 15C E7A10008
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x15C, 0xE7A10008);
    // 168
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x168, 0xE4400024);
    // 17C
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x17C, 0xE4810004);
    // 188
    PS2Memory::WriteEE<unsigned int>(routine_address + 0x188, 0xE4820008);

    PCSX2::recResetEE_stub();
#pragma endregion
}

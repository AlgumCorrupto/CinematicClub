#include "pch.h"

#include <cmath>
#include <Windows.h>
#include "types.h"
#include "mat3x4f.h"
#include "mathfunc.h"

#include "CDK.h"

#include "Playa.h"
#include "Game.h"
#include "Helpful.h"

using namespace PlayStation2;
using namespace sr2;
using namespace sr2::math;

// Statics
mat3x4f FreeCam::transform{};

bool frozen = false;

float FreeCam::moveSpeed = 25.f;

float FreeCam::yaw = 0.0f;
float FreeCam::pitch = 0.0f;
float FreeCam::tilt = 0.0f;

// --- Mouse look (FPS style with cinematic smoothing) ---
float FreeCam::smoothed_dx = 0.0f;
float FreeCam::smoothed_dy = 0.0f;
const float FreeCam::smoothing = 0.1f;  // smaller = smoother motion

// --- Smooth movement variables ---
vec3f FreeCam::velocity(0.0f, 0.0f, 0.0f);
const float FreeCam::accel = 10.f;      // acceleration factor
const float FreeCam::friction = 16.f;  // friction factor
std::vector<size_t> FreeCam::opponents = {};
unsigned char FreeCam::currentOpponent = 0;

float FreeCam::fov = 40.f; // degrees
float mouseElapsed = 33.f;
static bool firstFrame = true;
static const float NO_DAMPING_SPEED = 10.0;
float FreeCam::currentSpeed = 0.0f;
static vec3f lastMoveDir(0, 0, 0);

void FreeCam::Init(int cam_address)
{
    for (int row = 0; row < 4; row++) {        // rotation rows
        for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
            transform[row][col] = PS2Memory::ReadEE<float>(
                cam_address + (row * 3 + col) * sizeof(float)
            );
        }
    }
    fov = 40.f;

    yaw = atan2f(transform[2][0], transform[2][2]);
    pitch = atan2f(-transform[2][1], sqrtf(transform[2][0] * transform[2][0] + transform[2][2] * transform[2][2]));

    // --- Calculate forward and right vectors ---
    vec3f forward = {
        cosf(pitch) * sinf(yaw),
        -sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };

    vec3f up = transform[1]; // camera's up vector
    vec3f worldUp = { 0, 1, 0 }; // world up

    // Project worldUp onto the plane perpendicular to forward
    vec3f projectedUp = worldUp - forward * forward.dot(worldUp);
    projectedUp.normalize();

    // Compute angle between camera up and projected world up
    float tilt = atan2f(
        forward.cross(up).dot(projectedUp), // sin component
        up.dot(projectedUp)                 // cos component
    );
    opponents = Helpful::FindAllPatterns({ 0x00, 0x62, 0x76, 0x30 });
    currentOpponent = 0;
    firstFrame = true;
}


void FreeCam::Loop()
{
    const float fovMultiplier = 25.f;
    float boost = 1;
    // boost by pressing shift
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) boost = 5.0f;
    float mv = moveSpeed * boost;
    // move speed adjust
    if (GetAsyncKeyState('N') & 0x8000) DecrementMoveSpeed();
    if (GetAsyncKeyState('M') & 0x8000) IncrementMoveSpeed();

	// fov adjust
    if (GetAsyncKeyState('Z') & 0x8000) fov += fovMultiplier * Game::deltaTime;
    if (GetAsyncKeyState('X') & 0x8000) fov -= fovMultiplier * Game::deltaTime;

    if (Helpful::KeyPressedOnce('V')) DecrementCurrentOpponent();
    if (Helpful::KeyPressedOnce('B')) DecrementCurrentOpponent();
    if (Helpful::KeyPressedOnce('C')) MoveCurrentVehToCamera();

	if (GetAsyncKeyState('O') & 0x8000) tilt = 0.0; // reset tilt
	if (GetAsyncKeyState('Q') & 0x8000) tilt += .5f * Game::deltaTime; // roll left
	if (GetAsyncKeyState('E') & 0x8000) tilt -= .5f * Game::deltaTime; // roll right
    mouseElapsed += Game::deltaTime;

    // --- Mouse look ---
    if (Game::mouseLocked && !firstFrame)
    {
        RECT rect;
        GetWindowRect(Game::gameWindow, &rect);
        POINT center;
        center.x = (rect.right - rect.left) / 2;
        center.y = (rect.bottom - rect.top) / 2;
        ClientToScreen(Game::gameWindow, &center);

        POINT mp;
        GetCursorPos(&mp);

        float dx = float(mp.x - center.x);
        float dy = float(mp.y - center.y);

        if (Game::cinematicMode) {
            smoothed_dx = expDecay(smoothed_dx, dx, 2.5, Game::deltaTime);
            smoothed_dy = expDecay(smoothed_dy, dy, 2.5, Game::deltaTime);
        } else {
            smoothed_dx = dx;
            smoothed_dy = dy;
        }

        yaw -= smoothed_dx * Game::mouseSensitivity *  Game::deltaTime;
        pitch -= smoothed_dy * Game::mouseSensitivity * Game::deltaTime;

        SetCursorPos(center.x, center.y);
    }

    // --- Calculate forward and right vectors ---
    vec3f forward = {
        cosf(pitch) * sinf(yaw),
        -sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
    forward.normalize();

    vec3f right = {
        cosf(yaw),
        0,
        -sinf(yaw)
    };
    right.normalize();

    vec3f up = forward.cross(right);
    up.normalize();

    // --- Direct (parented) movement: no acceleration / friction ---
    vec3f moveDir(0, 0, 0);

    if (GetAsyncKeyState('W') & 0x8000) moveDir -= forward;
    if (GetAsyncKeyState('S') & 0x8000) moveDir += forward;
    if (GetAsyncKeyState('A') & 0x8000) moveDir -= right;
    if (GetAsyncKeyState('D') & 0x8000) moveDir += right;
    if (GetAsyncKeyState('R') & 0x8000) moveDir += up;
    if (GetAsyncKeyState('F') & 0x8000) moveDir -= up;

    bool hasInput = moveDir.lengthSq() > 0.0f;
    if (hasInput) {
        moveDir.normalize();
        lastMoveDir = moveDir;
    }
    float targetSpeed = hasInput ? mv : 0.0f;

    // critically damped exponential smoothing
    float response = hasInput ? accel : friction;
    currentSpeed += (targetSpeed - currentSpeed) * response * Game::deltaTime;


    // Immediate translation (parented camera behavior)
    if (currentSpeed > 0.0001f)
    {
        transform[3] += lastMoveDir * currentSpeed * Game::deltaTime;
    }

    mat3x4f rotY, rotX, rotZ, combined;

    // --- Build yaw + pitch rotation first ---
    rotation_y(rotY, yaw);
    rotation_x(rotX, pitch);

    // Combine yaw and pitch
    mat3x4f rotYP;
    mult(rotYP, rotX, rotY);

    // --- Apply roll around forward axis ---
    if (fabsf(tilt) > 0.0001f) {
        float c = cosf(tilt);
        float s = sinf(tilt);

        // Rotate right & up around forward
        vec3f newRight = right * c + up * s;
        vec3f newUp = up * c - right * s;

        right = newRight;
        up = newUp;
    }

    // --- Rebuild final rotation matrix ---
    combined[0] = right;
    combined[1] = up;
    combined[2] = forward;
    combined[3] = transform[3]; // translation stays the same
    transform = combined;
    firstFrame = false;
}


void FreeCam::IncrementMoveSpeed() {
    float factor = 1.0f + 2.5f * Game::deltaTime;
    moveSpeed *= factor;

    // Optional: prevent getting stuck at zero
    if (moveSpeed < 0.01f)
        moveSpeed = 0.01f;
}

void FreeCam::DecrementMoveSpeed() {
    float factor = 1.0f - 2.5f * Game::deltaTime;
    moveSpeed *= factor;

    if (moveSpeed < 0.0f)
        moveSpeed = 0.0f;
}

void FreeCam::MoveCurrentVehToCamera() {
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
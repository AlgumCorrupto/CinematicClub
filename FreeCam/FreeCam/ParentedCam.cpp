#include "pch.h"
#include "ParentedCam.h"
#include "OpponentCam.h"

#include <cmath>
#include <Windows.h>
#include "types.h"
#include "mat3x4f.h"
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
float ParentedCam::mouseSensitivity = 0.002f; // radians per pixel

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

float ParentedCam::fov = 40.f; // degrees

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
    // Guard: if we have no opponents, don't attempt to index into the vector
    if (opponents.empty()) {
        return;
    }

    // Ensure currentOpponent is still valid (in case opponents changed)
    if (currentOpponent >= opponents.size()) ParentedCam::SetOpponent(0);

    // Use explicit int addresses when calling PS2Memory helpers to avoid implicit narrowing warnings.
    size_t opponentAddr = opponents[currentOpponent];
    if (PS2Memory::ReadEE<unsigned int>(opponentAddr) != 0x00627630) {
        return;
    }

    // Read target transform
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            ParentedCam::targetTransform[row][col] = PS2Memory::ReadEE<float>(
                opponentAddr + 0x10 + (row * 3 + col) * static_cast<int>(sizeof(float))
            );
        }
    }

    const float fovMultiplier = 25.f;
    float boost = 1;
    // boost by pressing shift
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) boost = 5.0f;
    float mv = moveSpeed * boost * Game::deltaTime;
    // move speed adjust

    // Fix opponent cycling (safe now because we guard against empty)
    if (Helpful::KeyPressedOnce('T')) {
        if (currentOpponent == 0) ParentedCam::SetOpponent(static_cast<unsigned char>(opponents.size() - 1));
        else ParentedCam::SetOpponent(--currentOpponent);
    }
    if (Helpful::KeyPressedOnce('Y')) {
        ParentedCam::SetOpponent(static_cast<unsigned char>((currentOpponent + 1) % opponents.size()));
    }
    if (Helpful::KeyPressedOnce('G')) {
        ParentedCam::rotateWithVehicle = !ParentedCam::rotateWithVehicle;
    }

    // mouse sensitivity adjust
    if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) mouseSensitivity -= 0.0001f * Game::deltaTime;
    if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) mouseSensitivity += 0.0001f * Game::deltaTime;
    // fov adjust
    if (GetAsyncKeyState('Z') & 0x8000) fov += fovMultiplier * Game::deltaTime;
    if (GetAsyncKeyState('X') & 0x8000) fov -= fovMultiplier * Game::deltaTime;

    if (GetAsyncKeyState('N') & 0x8000) 
    { ParentedCam::DecrementMoveSpeed(); }

    if (GetAsyncKeyState('M') & 0x8000) 
    { ParentedCam::IncrementMoveSpeed();}

    if (GetAsyncKeyState('O') & 0x8000) tilt = 0.0; // reset tilt
    if (GetAsyncKeyState('Q') & 0x8000) tilt += .5f * Game::deltaTime; // roll left
    if (GetAsyncKeyState('E') & 0x8000) tilt -= .5f * Game::deltaTime; // roll right

    if (Game::mouseLocked == true) {
        // --- Get window center ---
        POINT center;
        {
            RECT rect;
            GetWindowRect(Game::gameWindow, &rect);
            center.x = (rect.right - rect.left) / 2;
            center.y = (rect.bottom - rect.top) / 2;
            ClientToScreen(Game::gameWindow, &center);
        }

        // --- Read mouse position ---
        POINT mousePos;
        GetCursorPos(&mousePos);

        // --- Delta from window center ---
        float dx = static_cast<float>(mousePos.x - center.x);
        float dy = static_cast<float>(mousePos.y - center.y);

        // --- Smooth or raw ---
        if (Game::cinematicMode) {
            smoothed_dx += (dx - smoothed_dx) * smoothing;
            smoothed_dy += (dy - smoothed_dy) * smoothing;
        }
        else {
            smoothed_dx = dx;
            smoothed_dy = dy;
        }

        // --- Apply rotation ---
        yaw -= smoothed_dx * mouseSensitivity;
        pitch -= smoothed_dy * mouseSensitivity;

        // --- Reset cursor ---
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

    vec3f targetVel(0.0f, 0.0f, 0.0f);
    velocity *= 0.f;

    // Movement keys
    if (GetAsyncKeyState('W') & 0x8000) velocity -= forward * mv;
    if (GetAsyncKeyState('S') & 0x8000) velocity += forward * mv;
    if (GetAsyncKeyState('A') & 0x8000) velocity -= right * mv;
    if (GetAsyncKeyState('D') & 0x8000) velocity += right * mv;
    if (GetAsyncKeyState('R') & 0x8000) velocity += up * mv;
    if (GetAsyncKeyState('F') & 0x8000) velocity -= up * mv;


    // --- Apply movement ---
    offset[3] += velocity;

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

    // --- Rebuild final camera-local rotation matrix ---
    combined[0] = right;   // camera local right basis
    combined[1] = up;      // camera local up basis
    combined[2] = forward; // camera local forward basis
    // combined[3] is the local translation (offset), but we'll handle it explicitly below

// --- Step 1: Vehicle basis ---
    vec3f vehicleRight = targetTransform[0];
    vec3f vehicleUp = targetTransform[1];
    vec3f vehicleFwd = targetTransform[2];

    // --- Step 2: Camera forward from yaw/pitch (after mouse) ---
    vec3f camForward = {
        cosf(pitch) * sinf(yaw),
        -sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
    camForward.normalize();

    // --- Step 3: Camera right/up ---
    vec3f camRight = { cosf(yaw), 0, -sinf(yaw) };
    vec3f camUp = camForward.cross(camRight).normalized();

    // --- Step 4: Apply tilt around forward ---
    if (fabsf(tilt) > 0.0001f)
    {
        float c = cosf(tilt);
        float s = sinf(tilt);
        vec3f newRight = camRight * c + camUp * s;
        vec3f newUp = camUp * c - camRight * s;
        camRight = newRight;
        camUp = newUp;
    }

    // --- Step 5: Transform to world space if vehicle-relative ---
    vec3f worldRight, worldUp, worldForward, worldOffset;
    if (rotateWithVehicle)
    {
        worldRight = vehicleRight * camRight.x + vehicleUp * camRight.y + vehicleFwd * camRight.z;
        worldUp = vehicleRight * camUp.x + vehicleUp * camUp.y + vehicleFwd * camUp.z;
        worldForward = vehicleRight * camForward.x + vehicleUp * camForward.y + vehicleFwd * camForward.z;

        worldOffset = vehicleRight * offset[3].x +
            vehicleUp * offset[3].y +
            vehicleFwd * offset[3].z;

        transform[0] = worldRight;
        transform[1] = worldUp;
        transform[2] = worldForward;
        transform[3] = targetTransform[3] + worldOffset;
    }
    else
    {
        // Free camera
        transform[0] = camRight;
        transform[1] = camUp;
        transform[2] = camForward;
        transform[3] = offset[3] + targetTransform[3];
    }
}

void ParentedCam::IncrementMoveSpeed() {
    moveSpeed += 25.f * Game::deltaTime;
}

void ParentedCam::DecrementMoveSpeed() {
    moveSpeed -= 25.f * Game::deltaTime;
    if (moveSpeed < 0.00f) moveSpeed = 0.00f;
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

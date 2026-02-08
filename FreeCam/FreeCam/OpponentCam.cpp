#include "pch.h"
#include "OpponentCam.h"
#include "ParentedCam.h"
#include "Helpful.h"
#include "types.h"
#include "mat3x4f.h"
#include "Game.h"
#include "mathfunc.h"

#include "CDK.h"

using namespace PlayStation2;
using namespace sr2;
using namespace sr2::math;

std::vector<size_t> OrbitCam::opponents = {};
mat3x4f OrbitCam::transform = {};

float OrbitCam::yaw = 0.0f;
float OrbitCam::pitch = 0.0f;
float OrbitCam::tilt = 0.0f;

float OrbitCam::smoothed_dx = 0.0f;
float OrbitCam::smoothed_dy = 0.0f;
const float OrbitCam::smoothing = .1f;

vec3f OrbitCam::velocity(0.0f, 0.0f, 0.0f);
const float OrbitCam::accel = 0.2f;
const float OrbitCam::friction = 0.15f;
mat3x4f OrbitCam::targetTransform{};
unsigned char OrbitCam::currentOpponent = 0;
float OrbitCam::polar = 0;
float OrbitCam::azimuthal = 0;
float OrbitCam::distance = 20.0f;
float OrbitCam::fov = 40.f;
static bool firstFrame = true;


bool OrbitCam::Init(int cam_address)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            transform[row][col] = PS2Memory::ReadEE<float>(
                cam_address + (row * 3 + col) * sizeof(float)
            );
        }
    }
	transform = mat3x4f::identity;

    opponents = {};
    currentOpponent = ParentedCam::currentOpponent;
    if (opponents.size() < currentOpponent) currentOpponent = 0;

    opponents = Helpful::FindAllPatterns({ 0x00, 0x62, 0x76, 0x30 });
    if (opponents.size() == 0) return false;

    fov = 40.f;
    distance = 20.0f;
    polar = 0.0f;
    azimuthal = 0.0f;
    firstFrame = true;


    return true;
}

// Helper function to create look-at matrix
mat3x4f createLookAt(const vec3f& eye, const vec3f& target, const vec3f& up)
{

    vec3f zaxis = (eye - target).normalized(); // Changed: eye - target (look FROM eye TO target)
    vec3f xaxis = up.cross(zaxis).normalized();
    vec3f yaxis = zaxis.cross(xaxis);

    mat3x4f result;
    result[0][0] = xaxis.x; result[0][1] = xaxis.y; result[0][2] = xaxis.z;
    result[1][0] = yaxis.x; result[1][1] = yaxis.y; result[1][2] = yaxis.z;
    result[2][0] = zaxis.x; result[2][1] = zaxis.y; result[2][2] = zaxis.z;
    result[3][0] = eye.x;   result[3][1] = eye.y;   result[3][2] = eye.z;

    return result;
}

// Helper clamping function
float clamp(float value, float minVal, float maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}
float OrbitCam::mouseElapsed = 0.f;
void OrbitCam::Loop() {
    if (opponents.empty()) return;
    mouseElapsed += Game::deltaTime;

    // Fix opponent cycling
    if (Helpful::KeyPressedOnce('Q')) {
        currentOpponent = (currentOpponent == 0) ? opponents.size() - 1 : currentOpponent - 1;
    }
    if (Helpful::KeyPressedOnce('E')) {
        currentOpponent = (currentOpponent + 1) % opponents.size();
    }

    // Validate opponent address
    size_t opponentAddr = opponents[currentOpponent];
    if (PS2Memory::ReadEE<unsigned int>(opponentAddr) != 0x00627630) {
        return;
    }

    // Read target transform
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            targetTransform[row][col] = PS2Memory::ReadEE<float>(
                opponentAddr + 0x10 + (row * 3 + col) * sizeof(float)
            );
        }
    }


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

        // Update angles with proper spherical coordinates
        polar -= smoothed_dx * Game::mouseSensitivity * Game::deltaTime;
        azimuthal += smoothed_dy * Game::mouseSensitivity * Game::deltaTime;

        // Keep polar in [0, 2π) range to prevent precision issues
        const float PI = 3.14159265f;
        const float TWO_PI = 2.0f * PI;
        if (polar > TWO_PI) polar -= TWO_PI;
        if (polar < 0) polar += TWO_PI;

        // Clamp azimuthal to prevent flipping (slightly

        SetCursorPos(center.x, center.y);
    }


    // Get target position
    vec3f targetPos = { targetTransform[3][0], targetTransform[3][1], targetTransform[3][2] };

    // Calculate camera position using PROPER spherical coordinates
    // In spherical coordinates:
    // x = r * sin(azimuthal) * cos(polar)
    // y = r * cos(azimuthal) 
    // z = r * sin(azimuthal) * sin(polar)
    // But we want to orbit around Y axis, so:
    float horizontalDistance = distance * cos(azimuthal);
    float verticalHeight = distance * sin(azimuthal);

    vec3f cameraPos = {
        targetPos.x + horizontalDistance * sin(polar),
        targetPos.y + verticalHeight,
        targetPos.z + horizontalDistance * cos(polar)
    };

    // Create look-at matrix (camera looks at target)
    vec3f up = { 0, 1, 0 };
    transform = createLookAt(cameraPos, targetPos, up);

    // Debug output to verify positions
    // printf("Camera: (%.2f, %.2f, %.2f) Target: (%.2f, %.2f, %.2f) Dist: %.2f\n", 
    //     cameraPos.x, cameraPos.y, cameraPos.z, 
    //     targetPos.x, targetPos.y, targetPos.z,
    //     distance);

    // FOV controls with clamping
    if (GetKeyState('Z') & 0x8000) {
        fov = fov + 50.0f * Game::deltaTime;
        if (fov < 10.0f) fov = 10.0f;
    }
    if (GetKeyState('X') & 0x8000) {
        fov = fov - 50.0f * Game::deltaTime;
        if (fov > 120.0f) fov = 120.0f;
    }

    // Distance controls with clamping
    if (GetKeyState('W') & 0x8000) {
        distance = distance - 25.f * Game::deltaTime;
        if (distance < 1.0f) distance = 1.0f;
    }
    if (GetKeyState('S') & 0x8000) {
        distance = distance + 25.f * Game::deltaTime;
        if (distance > 50.0f) distance = 50.0f;
    }
    firstFrame = false;
}
#include "pch.h"

#include <cmath>
#include <Windows.h>
#include "types.h"
#include "mat3x4f.h"

#include "CDK.h"

#include "Playa.h"
#include "Game.h"

using namespace PlayStation2;
using namespace sr2;
using namespace sr2::math;

// Statics
mat3x4f FreeCam::transform{};

bool frozen = false;

float FreeCam::moveSpeed = 25.f; // units per frame
float FreeCam::mouseSensitivity = 0.002f; // radians per pixel

float FreeCam::yaw = 0.0f;
float FreeCam::pitch = 0.0f;
float FreeCam::tilt = 0.0f;

// --- Mouse look (FPS style with cinematic smoothing) ---
float FreeCam::smoothed_dx = 0.0f;
float FreeCam::smoothed_dy = 0.0f;
const float FreeCam::smoothing = 0.1f;  // smaller = smoother motion

// --- Smooth movement variables ---
vec3f FreeCam::velocity(0.0f, 0.0f, 0.0f);
const float FreeCam::accel = 4.f;      // acceleration factor
const float FreeCam::friction = 0.015f;  // friction factor

float FreeCam::fov = 40.f; // degrees

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

    yaw   = atan2f(transform[2][0], transform[2][2]);
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
	// mouse sensitivity adjust
	if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) mouseSensitivity -= 0.0001f * Game::deltaTime;
	if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) mouseSensitivity += 0.0001f * Game::deltaTime;
	// fov adjust
    if (GetAsyncKeyState('Z') & 0x8000) fov += fovMultiplier * Game::deltaTime;
    if (GetAsyncKeyState('X') & 0x8000) fov -= fovMultiplier * Game::deltaTime;

	if (GetAsyncKeyState('O') & 0x8000) tilt = 0.0; // reset tilt
	if (GetAsyncKeyState('Q') & 0x8000) tilt += .5f * Game::deltaTime; // roll left
	if (GetAsyncKeyState('E') & 0x8000) tilt -= .5f * Game::deltaTime; // roll right
    if(Game::mouseLocked == true){
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

    // Movement keys
    if (GetAsyncKeyState('W') & 0x8000) targetVel -= forward * mv;
    if (GetAsyncKeyState('S') & 0x8000) targetVel += forward * mv;
    if (GetAsyncKeyState('A') & 0x8000) targetVel -= right * mv;
    if (GetAsyncKeyState('D') & 0x8000) targetVel += right * mv;
    if (GetAsyncKeyState('R') & 0x8000) targetVel += up * mv;
    if (GetAsyncKeyState('F') & 0x8000) targetVel -= up * mv;

    // --- Smooth velocity towards target ---
    velocity += (targetVel - velocity) * accel * Game::deltaTime;

    // --- Apply friction when no input ---
    if (targetVel.lengthSq() < 0.0001f) {
        velocity *= (1.0f - friction);
        if (velocity.lengthSq() < 0.00001f) velocity.zero();
    }

    // --- Apply movement ---
    transform[3] += velocity;

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
}

void FreeCam::IncrementMoveSpeed() {
    moveSpeed += 2.5f * Game::deltaTime;
}

void FreeCam::DecrementMoveSpeed() {
	moveSpeed -= 2.5f * Game::deltaTime;
	if (moveSpeed < 0.00f) moveSpeed = 0.00f;
}
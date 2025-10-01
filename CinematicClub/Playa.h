#pragma once

#include "pch.h"
#include <Windows.h>
#include "types.h"

using namespace sr2;

class Playa
{
public:
    static mat3x4f input;
    static mat3x4f output;

    // Settings
    static const float accel;
    static const float maxSpeed;
    static const float damping;
    static const float mouseSpeed;
    static const float moveSpeed;
    static const float rotSpeed;

    // State
    static float velX, velY, velZ; // velocity components
    static float yaw;              // rotation around Y axis
    static bool frozen;

    static void Init(int base_address); // load input matrix from memory
    static void ProcessEvents();        // read input (keyboard)
    static void Loop();                 // update movement each frame
};


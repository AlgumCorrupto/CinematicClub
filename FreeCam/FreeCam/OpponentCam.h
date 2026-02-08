#pragma once

#include "pch.h"
#include <Windows.h>
#include "types.h"
#include <vector>

using namespace sr2;

class OrbitCam
{
public:
    static mat3x4f transform;       // current camera transform
    static mat3x4f targetTransform; // transformation of the thing to look at and rotate around
    static float distance;          // distance to keep the camera
    static float polar;             // polar angle
    static float azimuthal;         // azimuthal
    static float mouseElapsed;

    static std::vector<size_t> opponents;
    static uint8_t currentOpponent;


    // State
    static float velX, velY, velZ; // velocity components
    static float yaw;              // rotation around Y axis
    static float pitch;            // rotation around X axis
    static float tilt;             // rotation around Z axis

    static float smoothed_dx; // smoothed mouse delta x
    static float smoothed_dy; // smoothed mouse delta y
    static const float smoothing; // smoothing factor for mouse

    const static float accel;    // acceleration factor
    const static float friction; // friction factor
    static sr2::vec3f velocity;  // current velocity vector

    static float fov; // current field of view (degrees)

    static bool Init(int cam_address); // load input matrix from memory
    static void Loop();                 // update movement each frame

    static void NextOpponent();
    static void PreviousOpponent();
};


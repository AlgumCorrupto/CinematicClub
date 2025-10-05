#pragma once

#include "pch.h"
#include <Windows.h>
#include "types.h"
#include <vector> // Add this include at the top of the file

using namespace sr2;

class Playa
{
public:
    static mat3x4f input;
    static mat3x4f output;

    static float moveSpeed;
	static float mouseSensitivity;

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

    static bool frozen;

    static void Init(int cam_address, int fov_address); // load input matrix from memory
    static void Loop();                 // update movement each frame

    static void IncrementMoveSpeed();
    static void DecrementMoveSpeed();
};


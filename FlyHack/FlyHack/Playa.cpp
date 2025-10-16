//"ENDEREÇO DA MEMÓRIA", "RÓTULO", "DESCRIÇÃO"
//"172A37B", "Car Z", "Up"
//"172A37F", "Car X", "Perpedicular to north axis"
//"172A377", "Car Y", "North Axis"
//
//
//search 00622DF8 for the car matrix
//search for 8D69000C for the routine that updates the car matrix

#include "pch.h"
#include <cmath>
#include <Windows.h>
#include "types.h"
#include "mat3x4f.h"

#include "CDK.h"

#include "Playa.h"


using namespace PlayStation2;
using namespace sr2;
using namespace sr2::math;

// Statics
mat3x4f Playa::input{};
mat3x4f Playa::output{};

bool frozen = false;

float Playa::moveSpeed = 0.5f; // units per frame

float Playa::yaw = 0.0f;
float startYaw = 0.0f;

// Helpers
static Mat4x4 MakeTranslation(float x, float y, float z)
{
    Mat4x4 t = Mat4x4::Identity();
    t(3, 0) = x;
    t(3, 1) = y;
    t(3, 2) = z;
    return t;
}

static Mat4x4 MakeRotationY(float angle)
{
    Mat4x4 r = Mat4x4::Identity();
    float c = cosf(angle);
    float s = sinf(angle);

    r(0, 0) = c;  r(0, 2) = s;
    r(2, 0) = -s;  r(2, 2) = c;

    return r;
}

void Playa::Init(int base_address)
{
    for (int row = 0; row < 4; row++) {        // rotation rows
        for (int col = 0; col < 3; col++) {    // 3 rotation + 1 translation
            input[row][col] = PS2Memory::ReadEE<float>(
                base_address + 0x10 + (row * 3 + col) * sizeof(float)
            );
        }
    }

    yaw = 0.0f;
    float fx = input[2][0]; // forward X
    float fz = input[2][2]; // forward 
    startYaw = yaw = atan2f(fx, fz);

    output = input; // copy to working transform
}

void Playa::Loop()
{
	// move speed adjust
    if (GetAsyncKeyState('N') & 0x8000) DecrementMoveSpeed();
    if (GetAsyncKeyState('M') & 0x8000) IncrementMoveSpeed();

	// move car forward/back/left/right
    vec3f forward = { sin(yaw), 0, cos(yaw) };
    if (GetAsyncKeyState('S') & 0x8000) output[3] += forward * moveSpeed;
    if (GetAsyncKeyState('W') & 0x8000) output[3] -= forward * moveSpeed;
 
    vec3f right = { cos(yaw), 0, -sin(yaw) };
    if (GetAsyncKeyState('A') & 0x8000) output[3] -= right * moveSpeed;
    if (GetAsyncKeyState('D') & 0x8000) output[3] += right * moveSpeed; 

	// move car up/down
    if (GetAsyncKeyState('F') & 0x8000) output[3][1] -= moveSpeed;
    if (GetAsyncKeyState('R') & 0x8000) output[3][1] += moveSpeed;

	// rotate car left/right
    float rotSpeed = 0.1f; // radians per frame
    if (GetAsyncKeyState('E') & 0x8000) yaw -= rotSpeed;
    if (GetAsyncKeyState('Q') & 0x8000) yaw += rotSpeed;

    rotation_y(output, yaw);
}

void Playa::IncrementMoveSpeed() {
    moveSpeed += 0.05f;

    printf("Move speed: %.2f\n", moveSpeed);
}

void Playa::DecrementMoveSpeed() {
	moveSpeed -= 0.05f;
	if (moveSpeed < 0.00f) moveSpeed = 0.00f;

    printf("Move speed: %.2f\n", moveSpeed);
}
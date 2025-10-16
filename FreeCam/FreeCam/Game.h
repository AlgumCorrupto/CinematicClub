#pragma once

#include "types.h"

class Game
{
public:
	const static unsigned int camRoutine;            // offset to the routine that return pointer to camera 3x4 matrix
    const static unsigned int fovInstruction;        // offset to the instruction that sets the fov
    const static unsigned int unusedMemory1;
    const static unsigned int unusedMemory2;
    

    static enum class Mode {
        unfrozen,
        free,
        opponent
    } mode;

	static POINT lockCenter;
	static HWND gameWindow;
    static bool mouseLocked;
    static float toSleep;
    static float deltaTime;

    static bool frozen;                        // if the camera is frozen
	static bool cinematicMode;                 // if mouse smoothing is enabled
    static unsigned int cam_base;              // camera 3x4 matrix base address is stored here
    static std::vector<float> fov_bases;
    static sr2::mat3x4f cameraTransform;
    static float fov;

    static void Init();             // initializes the game
    static void GetCamBase();       // fetches the car 3x4 matrix base address
    static void CameraFreeze();     // freezes the car
    static void CameraUnfreeze();   // unfreezes the car
    static void NextState();

    static void Loop();

    static void LockAndHideMouse(HWND hwnd);
	static void UnlockAndShowMouse();
private:
    static void Detect();           // detects the game version
};
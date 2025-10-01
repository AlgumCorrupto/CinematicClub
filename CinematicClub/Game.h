#pragma once
class Game
{
public:
    const static unsigned int cam_begin_pattern = 0x18046300;

    //static unsigned int instruction_address;
    static unsigned int routine_address;
    static unsigned int car_base;// camera 4x4 matrix base address is stored here

    static bool frozen;

    static void Init();             // initializes the game
    static void GetCarBase();    // fetches the camera 4x4 matrix base address
    static void CameraFreeze();     // freezes the camera
    static void CameraUnfreeze();   // unfreezes the camera

    static void Loop();

private:
    static void Detect();           // detects the game version
};


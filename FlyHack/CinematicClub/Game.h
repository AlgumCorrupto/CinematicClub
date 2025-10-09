#pragma once
class Game
{
public:
    const static unsigned int cam_begin_pattern = 0x18046300;

    //static unsigned int instruction_address;
    static unsigned int routine_address;
    static unsigned int car_base;// camera 4x4 matrix base address is stored here

    static bool frozen;
    static unsigned char car_to_select;

    static void Init();             // initializes the game
    static void GetCarBase();    // fetches the car 3x4 matrix base address
    static void CarFreeze();     // freezes the car
    static void CarUnfreeze();   // unfreezes the car

    static void Increment_car_to_select();
    static void Decrement_car_to_select();

    static void Loop();

private:
    static void Detect();           // detects the game version
};


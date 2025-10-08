#pragma once
#include <vector>

class Game
{
public:
	enum class Conversion
	{
		mph,
		kph
	};
	static Conversion speedConversion;
    static unsigned int velocityBase;
	const static unsigned int formatted_string_addr;
    static unsigned int playerBase;
	static bool active;

    static void Init();             // initializes the game
    static void Loop();             
	static void CheckWhereVelocityIsStored(); // checks where the velocity is stored in memory
};
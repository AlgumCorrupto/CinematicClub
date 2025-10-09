#include "pch.h"
#include "CDK.h"
#include "Game.h"
#include <chrono>

static bool running = true;

using namespace PlayStation2;

struct PeriodicExecutor {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    double intervalSeconds;
    TimePoint lastExecution;

    PeriodicExecutor(double seconds)
        : intervalSeconds(seconds), lastExecution(Clock::now()) {}

    // Returns true if interval has elapsed, and resets timer
    bool ShouldRun() {
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastExecution).count();
        if (elapsed >= intervalSeconds) {
            lastExecution = now;
            return true;
        }
        return false;
    }
};

DWORD WINAPI ClientThread(LPVOID hInstance) {
    running = InitCDK();
    printf("AlgumCorrupto presents...................\n");
    printf("====================================================\n");
    printf("= DigitalSpeedometer v1 from CinematicClub modpack =\n");
    printf("====================================================\n");

    printf("\n\nMain repo: https://github.com/AlgumCorrupto/CinematicClub\n");

    PeriodicExecutor executor(10.0); // 10 seconds interval
    Game::Init();

    while (running) {
        try {
            Sleep(33); // roughly 30 FPS

			if (PS2Memory::ReadEE<unsigned int>(0x6144BC) != 0x0000000) continue; // if game is loading, skip

            if (executor.ShouldRun()
                && (PS2Memory::ReadEE<unsigned int>(Game::playerBase) != 0x627760)
                    || PS2Memory::ReadEE<unsigned int>(Game::velocityBase) != 0x6278F8)
                        Game::CheckWhereVelocityIsStored();

            Game::Loop();
            if (GetAsyncKeyState(VK_END))     running = false;
        }
        catch (...) {}
    }

    PS2Memory::WriteEE<unsigned int>(0x006F60B0, false); // disable debug display
    ShutdownCDK();
    FreeLibraryAndExitThread((HMODULE)hInstance, EXIT_SUCCESS);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)ClientThread, hModule, NULL, NULL);
        break;
    case DLL_PROCESS_DETACH: running = false;
        break;
    }
    return TRUE;
}
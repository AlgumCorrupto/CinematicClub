#include "pch.h"
#include "CDK.h"
#include "Game.h"

static bool running = true;

using namespace PlayStation2;


DWORD WINAPI ClientThread(LPVOID hInstance) {
    running = InitCDK();
    Game::Init();
    printf("AlgumCorrupto presents...................\n");
    printf("=========================================\n");
    printf("= FreeCam v1 from CinematicClub modpack =\n");
    printf("=========================================\n");

    printf("\n\nMain repo: https://github.com/AlgumCorrupto/CinematicClub\n");

    while (running) {
        if (PS2Memory::ReadEE<unsigned int>(0x6144BC) != 0x0000000) continue; // if game is loading, skip
        Sleep(Game::toSleep);
        Game::Loop();
        if (GetAsyncKeyState(VK_END))     running = false;
    }

	Game::CameraUnfreeze();
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


// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "CDK.h"
#include "Game.h"

static bool running = true;

//static unsigned int instruction_address   = 0x00518fe8;
//static unsigned int cam_begin_pattern     = 0x18046300;

using namespace PlayStation2;


DWORD WINAPI ClientThread(LPVOID hInstance) {
    running = InitCDK();
    Game::Init();
    printf("AlgumCorrupto presents...................\n");
	printf("=========================================\n");
    printf("= FlyHack v1 from CinematicClub modpack =\n");
    printf("=========================================\n");

    printf("\n\nMain repo: https://github.com/AlgumCorrupto/CinematicClub\n");

    while (running) {
        Game::Loop();
        if (GetAsyncKeyState(VK_END))     running = false;
    }
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


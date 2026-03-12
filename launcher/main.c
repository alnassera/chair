/*
 * CHAIR launcher — starts audio-engine.exe and chair-overlay.exe,
 * forwards CLI args to the engine, tears down both on exit.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HANDLE g_engineProc = NULL;
static HANDLE g_overlayProc = NULL;

static void Cleanup(void)
{
    if (g_overlayProc) { TerminateProcess(g_overlayProc, 0); CloseHandle(g_overlayProc); }
    if (g_engineProc)  { TerminateProcess(g_engineProc, 0);  CloseHandle(g_engineProc); }
}

static BOOL WINAPI CtrlHandler(DWORD type)
{
    (void)type;
    printf("\nShutting down CHAIR...\n");
    Cleanup();
    ExitProcess(0);
    return TRUE;
}

/* Launch a process in the same directory as this exe. Returns process handle or NULL. */
static HANDLE Launch(const char *exeDir, const char *exeName, const char *extraArgs, BOOL hidden)
{
    char cmdLine[MAX_PATH * 2];
    if (extraArgs && extraArgs[0])
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\\%s\" %s", exeDir, exeName, extraArgs);
    else
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\\%s\"", exeDir, exeName);

    STARTUPINFOA si = { .cb = sizeof(si) };
    if (hidden)
    {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION pi = {0};
    DWORD flags = hidden ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE;
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, flags, NULL, exeDir, &si, &pi))
    {
        fprintf(stderr, "Failed to launch %s (error %lu)\n", exeName, GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

int main(int argc, char **argv)
{
    /* Find our own directory */
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *lastSlash = '\0';

    /* Build extra args string for the engine (forward everything after argv[0]) */
    char engineArgs[2048] = {0};
    for (int i = 1; i < argc; i++)
    {
        if (i > 1) strcat(engineArgs, " ");
        /* Quote args that contain spaces */
        if (strchr(argv[i], ' '))
        {
            strcat(engineArgs, "\"");
            strcat(engineArgs, argv[i]);
            strcat(engineArgs, "\"");
        }
        else
        {
            strcat(engineArgs, argv[i]);
        }
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    SetConsoleTitleA("CHAIR");

    printf("CHAIR - Audio Direction Overlay\n");
    printf("===============================\n\n");

    /* 1. Launch audio engine (gets its own console window) */
    printf("Starting audio engine...\n");
    g_engineProc = Launch(exePath, "audio-engine.exe", engineArgs, FALSE);
    if (!g_engineProc)
    {
        fprintf(stderr, "Could not start audio engine. Is audio-engine.exe in the same folder?\n");
        return 1;
    }

    /* 2. Brief pause for engine to initialize capture */
    Sleep(1500);

    /* Check engine didn't crash on startup */
    DWORD exitCode;
    if (GetExitCodeProcess(g_engineProc, &exitCode) && exitCode != STILL_ACTIVE)
    {
        fprintf(stderr, "Audio engine exited immediately (code %lu). Check your audio setup.\n", exitCode);
        CloseHandle(g_engineProc);
        g_engineProc = NULL;
        return 1;
    }

    /* 3. Launch overlay (no console window — it's a WPF GUI) */
    printf("Starting overlay...\n");
    g_overlayProc = Launch(exePath, "chair-overlay.exe", NULL, TRUE);
    if (!g_overlayProc)
    {
        fprintf(stderr, "Could not start overlay. Is chair-overlay.exe in the same folder?\n");
        Cleanup();
        return 1;
    }

    printf("\nCHAIR is running.\n");
    printf("  Ctrl+Shift+Q  — close overlay\n");
    printf("  Ctrl+C here   — stop everything\n\n");

    /* 4. Wait for either process to exit */
    HANDLE handles[2] = { g_engineProc, g_overlayProc };
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

    if (result == WAIT_OBJECT_0)
        printf("Audio engine stopped.\n");
    else if (result == WAIT_OBJECT_0 + 1)
        printf("Overlay closed.\n");

    /* Tear down the other one */
    Cleanup();
    printf("CHAIR stopped.\n");
    return 0;
}

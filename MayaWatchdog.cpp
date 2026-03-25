#include <winsock2.h>   // must be before windows.h
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>

#pragma comment(lib, "user32.lib")

// ------------------------------------------------------------
// CONFIG
// ------------------------------------------------------------
const int CHECK_INTERVAL_MS = 5000;   // recheck every 5 sec
const int HUNG_TIMEOUT_MS   = 2000;   // UI response timeout

// ------------------------------------------------------------
// Reliable Freeze Detection
// ------------------------------------------------------------
bool IsWindowReallyHung(HWND hwnd)
{
    DWORD_PTR result = 0;

    LRESULT res = SendMessageTimeout(
        hwnd,
        WM_NULL,
        0,
        0,
        SMTO_ABORTIFHUNG,
        HUNG_TIMEOUT_MS,
        &result
    );

    return res == 0; // timeout = frozen
}

// ------------------------------------------------------------
// Ask User Confirmation
// ------------------------------------------------------------
bool AskUserConfirmation(DWORD pid)
{
    std::string message =
        "Maya appears to be frozen.\n\n"
        "PID: " + std::to_string(pid) +
        "\n\nCrash this Maya instance?";

    int result = MessageBoxA(
        NULL,
        message.c_str(),
        "Maya Watchdog",
        MB_ICONWARNING | MB_YESNO | MB_TOPMOST | MB_SYSTEMMODAL
    );

    return result == IDYES;
}

// ------------------------------------------------------------
// Crash (Terminate) Maya Process
// ------------------------------------------------------------
bool CrashMaya(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process)
    {
        std::cout << "[Watchdog] Failed to open process.\n";
        return false;
    }

    BOOL ok = TerminateProcess(process, 1);
    CloseHandle(process);

    return ok;
}

// ------------------------------------------------------------
// Collect all maya.exe PIDs
// ------------------------------------------------------------
std::vector<DWORD> GetMayaProcesses()
{
    std::vector<DWORD> pids;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do
        {
            if (_stricmp(pe.szExeFile, "maya.exe") == 0)
                pids.push_back(pe.th32ProcessID);

        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return pids;
}

// ------------------------------------------------------------
// Find Main Window of Process
// ------------------------------------------------------------
struct HandleData
{
    DWORD pid;
    HWND hwnd;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    HandleData& data = *(HandleData*)lParam;

    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid != data.pid)
        return TRUE;

    if (!IsWindowVisible(hwnd))
        return TRUE;

    data.hwnd = hwnd;
    return FALSE;
}

HWND FindMainWindow(DWORD pid)
{
    HandleData data;
    data.pid = pid;
    data.hwnd = NULL;

    EnumWindows(EnumWindowsCallback, (LPARAM)&data);
    return data.hwnd;
}

// ------------------------------------------------------------
// Watchdog Loop
// ------------------------------------------------------------
void WatchdogLoop()
{
    std::cout << "Maya Watchdog running...\n";

    while (true)
    {
        auto mayaPids = GetMayaProcesses();

        for (DWORD pid : mayaPids)
        {
            HWND hwnd = FindMainWindow(pid);
            if (!hwnd)
                continue;

            if (IsWindowReallyHung(hwnd))
            {
                std::cout << "[Watchdog] Frozen Maya detected (PID "
                          << pid << ")\n";

                // Ask every cycle until user decides YES
                if (AskUserConfirmation(pid))
                {
                    std::cout << "[Watchdog] User approved crash.\n";

                    if (CrashMaya(pid))
                        std::cout << "[Watchdog] Maya crashed successfully.\n";
                    else
                        std::cout << "[Watchdog] Failed to crash Maya.\n";
                }
                else
                {
                    std::cout << "[Watchdog] User chose NO. Will retry later.\n";
                }
            }
        }

        Sleep(CHECK_INTERVAL_MS);
    }
}

// ------------------------------------------------------------
// Entry Point
// ------------------------------------------------------------
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    WatchdogLoop();
    return 0;
}

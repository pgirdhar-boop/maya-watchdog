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
const int CHECK_INTERVAL_MS = 5000;   // check every 5 sec
const int HUNG_TIMEOUT_MS   = 2000;   // UI response timeout

// ------------------------------------------------------------
// Reliable Freeze Detection (Studio Method)
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

    return res == 0;
}

// ------------------------------------------------------------
// Kill Process By PID
// ------------------------------------------------------------
bool KillProcess(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess)
        return false;

    TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return true;
}

// ------------------------------------------------------------
// Restart Maya
// (Uses system PATH maya.exe)
// ------------------------------------------------------------
void RestartMaya()
{
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcess(
            NULL,
            (LPSTR)"maya.exe",
            NULL, NULL, FALSE,
            0, NULL, NULL,
            &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "[Watchdog] Maya restarted.\n";
    }
    else
    {
        std::cout << "[Watchdog] Failed to restart Maya.\n";
    }
}

// ------------------------------------------------------------
// Collect all maya.exe PIDs
// ------------------------------------------------------------
std::vector<DWORD> GetMayaProcesses()
{
    std::vector<DWORD> pids;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snapshot, &pe))
    {
        do
        {
            if (_stricmp(pe.szExeFile, "maya.exe") == 0)
            {
                pids.push_back(pe.th32ProcessID);
            }
        }
        while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pids;
}

// ------------------------------------------------------------
// Find main window for PID
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
    std::cout << "Maya Watchdog started...\n";

    while (true)
    {
        std::vector<DWORD> mayaPids = GetMayaProcesses();

        for (DWORD pid : mayaPids)
        {
            HWND hwnd = FindMainWindow(pid);
            if (!hwnd)
                continue;

            if (IsWindowReallyHung(hwnd))
            {
                std::cout << "[Watchdog] Frozen Maya detected (PID "
                          << pid << ")\n";

                if (KillProcess(pid))
                {
                    std::cout << "[Watchdog] Maya terminated.\n";
                    Sleep(2000);
                    RestartMaya();
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

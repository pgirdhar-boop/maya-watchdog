#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <map>

#pragma comment(lib, "psapi.lib")

// ---------------- SETTINGS ----------------

const int CHECK_INTERVAL_MS = 5000;
const int STARTUP_GRACE_SEC = 60;

// ------------------------------------------

struct MayaSession
{
    DWORD pid;
    HWND hwnd;
    DWORD startTick;
};

std::map<DWORD, MayaSession> sessions;

// ------------------------------------------------------------
// Find Maya main window
// ------------------------------------------------------------

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    char title[512];
    GetWindowTextA(hwnd, title, sizeof(title));

    if (strstr(title, "Autodesk Maya"))
    {
        MayaSession session;
        session.pid = pid;
        session.hwnd = hwnd;
        session.startTick = GetTickCount();

        sessions[pid] = session;
    }
    return TRUE;
}

// ------------------------------------------------------------
// Get CPU usage snapshot
// ------------------------------------------------------------

bool IsCpuIdle(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return false;

    FILETIME ftCreation, ftExit, ftKernel1, ftUser1;
    FILETIME ftKernel2, ftUser2;

    GetProcessTimes(process, &ftCreation, &ftExit, &ftKernel1, &ftUser1);
    Sleep(500);
    GetProcessTimes(process, &ftCreation, &ftExit, &ftKernel2, &ftUser2);

    ULONGLONG k1 = ((ULONGLONG)ftKernel1.dwHighDateTime << 32) | ftKernel1.dwLowDateTime;
    ULONGLONG k2 = ((ULONGLONG)ftKernel2.dwHighDateTime << 32) | ftKernel2.dwLowDateTime;

    CloseHandle(process);

    return (k2 - k1) < 10000;
}

// ------------------------------------------------------------
// Extract Maya opened file
// ------------------------------------------------------------

std::string GetWindowTitle(HWND hwnd)
{
    char title[512];
    GetWindowTextA(hwnd, title, sizeof(title));
    return std::string(title);
}

// ------------------------------------------------------------
// Crash Maya safely (creates recovery file)
// ------------------------------------------------------------

bool CrashPid(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) return false;

    void crash()
    {
        *((int*)0) = 0;
    }

    LPVOID mem = VirtualAllocEx(
        process,
        NULL,
        256,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);

    if (!mem)
    {
        CloseHandle(process);
        return false;
    }

    WriteProcessMemory(
        process,
        mem,
        reinterpret_cast<LPCVOID>(crash),
        256,
        NULL);

    HANDLE thread = CreateRemoteThread(
        process,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)mem,
        NULL,
        0,
        NULL);

    if (thread) CloseHandle(thread);

    CloseHandle(process);
    return true;
}

// ------------------------------------------------------------
// Confirmation dialog
// ------------------------------------------------------------

bool AskUser(std::string name)
{
    std::string msg =
        "Maya appears frozen:\n\n" +
        name +
        "\n\nCrash Maya and create recovery file?";

    int result = MessageBoxA(
        NULL,
        msg.c_str(),
        "Maya Watchdog",
        MB_YESNO | MB_ICONWARNING | MB_TOPMOST);

    return result == IDYES;
}

// ------------------------------------------------------------
// Detect running maya.exe
// ------------------------------------------------------------

void ScanMayaProcesses()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do
        {
            if (_stricmp(pe.szExeFile, "maya.exe") == 0)
            {
                if (sessions.find(pe.th32ProcessID) == sessions.end())
                {
                    EnumWindows(EnumWindowsProc, 0);
                }
            }
        }
        while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
}

// ------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    MessageBoxA(NULL,
        "Maya Watchdog running in background.",
        "Maya Watchdog",
        MB_OK | MB_ICONINFORMATION);

    while (true)
    {
        ScanMayaProcesses();

        for (auto& it : sessions)
        {
            MayaSession& s = it.second;

            // Ignore startup phase
            DWORD aliveTime =
                (GetTickCount() - s.startTick) / 1000;

            if (aliveTime < STARTUP_GRACE_SEC)
                continue;

            if (!IsWindow(s.hwnd))
                continue;

            if (!IsHungAppWindow(s.hwnd))
                continue;

            if (!IsCpuIdle(s.pid))
                continue;

            std::string title = GetWindowTitle(s.hwnd);

            if (AskUser(title))
                CrashPid(s.pid);
        }

        Sleep(CHECK_INTERVAL_MS);
    }

    return 0;
}

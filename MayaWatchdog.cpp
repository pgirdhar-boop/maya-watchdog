#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <map>

#pragma comment(lib, "psapi.lib")

// --------------------------------------------------
// SETTINGS
// --------------------------------------------------

const int CHECK_INTERVAL_MS = 5000;
const int STARTUP_GRACE_SEC = 60;

// --------------------------------------------------

struct MayaSession
{
    DWORD pid;
    HWND hwnd;
    DWORD startTick;
};

std::map<DWORD, MayaSession> sessions;

// --------------------------------------------------
// CRASH PAYLOAD (must be GLOBAL, not inside function)
// --------------------------------------------------

DWORD WINAPI CrashThread(LPVOID)
{
    // intentional access violation
    volatile int* p = nullptr;
    *p = 0;
    return 0;
}

// --------------------------------------------------
// ENUM WINDOWS
// --------------------------------------------------

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    char title[512];
    GetWindowTextA(hwnd, title, sizeof(title));

    if (strstr(title, "Autodesk Maya"))
    {
        MayaSession s;
        s.pid = pid;
        s.hwnd = hwnd;
        s.startTick = GetTickCount();

        sessions[pid] = s;
    }
    return TRUE;
}

// --------------------------------------------------
// CPU IDLE CHECK
// --------------------------------------------------

bool IsCpuIdle(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) return false;

    FILETIME c,e,k1,u1,k2,u2;

    GetProcessTimes(process,&c,&e,&k1,&u1);
    Sleep(500);
    GetProcessTimes(process,&c,&e,&k2,&u2);

    ULONGLONG a =
        ((ULONGLONG)k1.dwHighDateTime<<32)|k1.dwLowDateTime;
    ULONGLONG b =
        ((ULONGLONG)k2.dwHighDateTime<<32)|k2.dwLowDateTime;

    CloseHandle(process);

    return (b-a) < 10000;
}

// --------------------------------------------------
// GET WINDOW TITLE (SCENE NAME)
// --------------------------------------------------

std::string GetWindowTitle(HWND hwnd)
{
    char title[512];
    GetWindowTextA(hwnd, title, sizeof(title));
    return std::string(title);
}

// --------------------------------------------------
// SAFE CRASH (creates Maya recovery)
// --------------------------------------------------

bool CrashPid(DWORD pid)
{
    HANDLE process =
        OpenProcess(PROCESS_CREATE_THREAD |
                    PROCESS_VM_OPERATION |
                    PROCESS_VM_WRITE,
                    FALSE, pid);

    if (!process) return false;

    HANDLE thread =
        CreateRemoteThread(
            process,
            NULL,
            0,
            CrashThread,
            NULL,
            0,
            NULL);

    if (thread) CloseHandle(thread);

    CloseHandle(process);
    return true;
}

// --------------------------------------------------
// CONFIRMATION DIALOG
// --------------------------------------------------

bool AskUser(const std::string& title)
{
    std::string msg =
        "Maya appears frozen:\n\n" +
        title +
        "\n\nCrash Maya and create recovery file?";

    int result = MessageBoxA(
        NULL,
        msg.c_str(),
        "Maya Watchdog",
        MB_YESNO | MB_ICONWARNING | MB_TOPMOST);

    return result == IDYES;
}

// --------------------------------------------------
// SCAN MAYA
// --------------------------------------------------

void ScanMaya()
{
    HANDLE snap =
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if(Process32First(snap,&pe))
    {
        do
        {
            if(!_stricmp(pe.szExeFile,"maya.exe"))
            {
                EnumWindows(EnumWindowsProc,0);
            }
        }
        while(Process32Next(snap,&pe));
    }

    CloseHandle(snap);
}

// --------------------------------------------------
// MAIN
// --------------------------------------------------

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
    MessageBoxA(NULL,
        "Maya Watchdog running in background.",
        "Maya Watchdog",
        MB_OK|MB_ICONINFORMATION);

    while(true)
    {
        ScanMaya();

        for(auto& it : sessions)
        {
            MayaSession& s = it.second;

            DWORD alive =
                (GetTickCount()-s.startTick)/1000;

            if(alive < STARTUP_GRACE_SEC)
                continue;

            if(!IsWindow(s.hwnd))
                continue;

            if(!IsHungAppWindow(s.hwnd))
                continue;

            if(!IsCpuIdle(s.pid))
                continue;

            std::string title =
                GetWindowTitle(s.hwnd);

            if(AskUser(title))
                CrashPid(s.pid);
        }

        Sleep(CHECK_INTERVAL_MS);
    }

    return 0;
}

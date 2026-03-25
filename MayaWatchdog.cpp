#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <iostream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON (WM_USER + 1)

NOTIFYICONDATA nid;
HWND g_hwnd;

// ------------------------------------------------------------
// SAFE CRASH FUNCTION (creates Maya recovery file)
// ------------------------------------------------------------
void crash()
{
    *(int*)0 = 0;
}

bool CrashPid(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) return false;

    LPVOID mem = VirtualAllocEx(
        process, NULL, 256,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE);

    if (!mem) {
        CloseHandle(process);
        return false;
    }

    WriteProcessMemory(process, mem, crash, 256, NULL);

    HANDLE thread = CreateRemoteThread(
        process,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)mem,
        NULL,
        0,
        NULL);

    if (!thread) {
        CloseHandle(process);
        return false;
    }

    CloseHandle(thread);
    CloseHandle(process);
    return true;
}

// ------------------------------------------------------------
// CHECK WINDOW RESPONSIVENESS (REAL FREEZE DETECTION)
// ------------------------------------------------------------
bool IsWindowFrozen(HWND hwnd)
{
    DWORD_PTR result;

    LRESULT res = SendMessageTimeout(
        hwnd,
        WM_NULL,
        0,
        0,
        SMTO_ABORTIFHUNG,
        3000,
        &result);

    return res == 0;
}

// ------------------------------------------------------------
// IGNORE MAYA DURING STARTUP
// ------------------------------------------------------------
bool IsNewProcess(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return false;

    FILETIME create, exit, kernel, user;
    GetProcessTimes(h, &create, &exit, &kernel, &user);
    CloseHandle(h);

    ULONGLONG start =
        ((ULONGLONG)create.dwHighDateTime << 32) |
        create.dwLowDateTime;

    ULONGLONG now =
        GetTickCount64() * 10000ULL;

    // ignore first 120 seconds
    return (now - start) < 1200000000ULL;
}

// ------------------------------------------------------------
// GET PROCESS NAME
// ------------------------------------------------------------
bool IsMayaProcess(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    char path[MAX_PATH];
    DWORD size = MAX_PATH;

    bool result = false;

    if (QueryFullProcessImageNameA(h, 0, path, &size))
    {
        std::string exe(path);
        if (exe.find("maya.exe") != std::string::npos)
            result = true;
    }

    CloseHandle(h);
    return result;
}

// ------------------------------------------------------------
// CONFIRMATION DIALOG
// ------------------------------------------------------------
bool AskConfirmation(DWORD pid)
{
    std::string msg =
        "Maya appears frozen.\n\nPID: "
        + std::to_string(pid) +
        "\n\nCrash Maya to create recovery file?";

    int r = MessageBoxA(
        NULL,
        msg.c_str(),
        "Maya Watchdog",
        MB_ICONWARNING | MB_YESNO | MB_TOPMOST);

    return r == IDYES;
}

// ------------------------------------------------------------
// ENUM WINDOWS
// ------------------------------------------------------------
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    if (!IsMayaProcess(pid))
        return TRUE;

    if (IsNewProcess(pid))
        return TRUE;

    if (IsWindowVisible(hwnd) && IsWindowFrozen(hwnd))
    {
        *((DWORD*)lParam) = pid;
        return FALSE;
    }

    return TRUE;
}

// ------------------------------------------------------------
// WATCHDOG LOOP
// ------------------------------------------------------------
DWORD WINAPI WatchdogThread(LPVOID)
{
    while (true)
    {
        DWORD frozenPid = 0;

        EnumWindows(EnumWindowsProc, (LPARAM)&frozenPid);

        if (frozenPid)
        {
            if (AskConfirmation(frozenPid))
            {
                CrashPid(frozenPid);
                Sleep(10000); // cooldown
            }
            else
            {
                Sleep(15000); // retry later
            }
        }

        Sleep(5000);
    }

    return 0;
}

// ------------------------------------------------------------
// TRAY WINDOW
// ------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRAYICON && lParam == WM_RBUTTONUP)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenu(menu, MF_STRING, 1, "Exit");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);

        int cmd = TrackPopupMenu(
            menu,
            TPM_RETURNCMD,
            pt.x, pt.y,
            0,
            hwnd,
            NULL);

        if (cmd == 1)
            PostQuitMessage(0);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ------------------------------------------------------------
// ENTRY POINT
// ------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "MayaWatchdog";

    RegisterClass(&wc);

    g_hwnd = CreateWindow(
        "MayaWatchdog",
        "",
        0,
        0,0,0,0,
        NULL,NULL,hInst,NULL);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    strcpy_s(nid.szTip, "Maya Watchdog Running");

    Shell_NotifyIcon(NIM_ADD, &nid);

    CreateThread(NULL, 0, WatchdogThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    return 0;
}

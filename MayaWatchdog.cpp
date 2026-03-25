#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <algorithm>

#define WM_TRAYICON (WM_USER + 1)

struct MayaSession
{
    DWORD pid;
    HWND hwnd;
    std::string title;
    bool frozen;
};

std::vector<MayaSession> sessions;
NOTIFYICONDATA nid;
HWND g_hwnd;

//////////////////////////////////////////////////////////////
// case-insensitive contains
//////////////////////////////////////////////////////////////
bool containsMaya(const char* name)
{
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.find("maya") != std::string::npos;
}

//////////////////////////////////////////////////////////////
// Detect freeze using Windows message timeout
//////////////////////////////////////////////////////////////
bool IsWindowFrozen(HWND hwnd)
{
    DWORD_PTR result;
    LRESULT r = SendMessageTimeout(
        hwnd,
        WM_NULL,
        0,
        0,
        SMTO_ABORTIFHUNG,
        2000,
        &result);

    return r == 0;
}

//////////////////////////////////////////////////////////////
// Safe Maya crash (creates recovery file)
//////////////////////////////////////////////////////////////
void CrashMaya(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) return;

    auto crash = []() { *(int*)0 = 0; };

    LPVOID mem = VirtualAllocEx(process, NULL, 256,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);

    if (!mem) return;

    WriteProcessMemory(
        process,
        mem,
        reinterpret_cast<LPCVOID>(crash),
        256,
        NULL);

    CreateRemoteThread(
        process,
        NULL,
        0,
        (LPTHREAD_START_ROUTINE)mem,
        NULL,
        0,
        NULL);
}

//////////////////////////////////////////////////////////////
// Enumerate Maya windows
//////////////////////////////////////////////////////////////
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    if (!IsWindowVisible(hwnd))
        return TRUE;

    char title[512];
    GetWindowTextA(hwnd, title, 512);

    if (strlen(title) == 0)
        return TRUE;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do {
            if (pe.th32ProcessID == pid &&
                containsMaya(pe.szExeFile))
            {
                MayaSession s;
                s.pid = pid;
                s.hwnd = hwnd;
                s.title = title;
                s.frozen = IsWindowFrozen(hwnd);
                sessions.push_back(s);
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return TRUE;
}

//////////////////////////////////////////////////////////////
// Scan Maya sessions
//////////////////////////////////////////////////////////////
void ScanMaya()
{
    sessions.clear();
    EnumWindows(EnumWindowsProc, 0);
}

//////////////////////////////////////////////////////////////
// Tray icon color
//////////////////////////////////////////////////////////////
void UpdateTrayIcon()
{
    bool frozen = false;

    for (auto& s : sessions)
        if (s.frozen) frozen = true;

    nid.hIcon = LoadIcon(
        NULL,
        frozen ? IDI_ERROR : IDI_APPLICATION);

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

//////////////////////////////////////////////////////////////
// Crash frozen sessions
//////////////////////////////////////////////////////////////
void CrashFrozen()
{
    for (auto& s : sessions)
        if (s.frozen)
            CrashMaya(s.pid);
}

//////////////////////////////////////////////////////////////
// Show sessions popup
//////////////////////////////////////////////////////////////
void ShowSessions()
{
    std::string text;

    if (sessions.empty())
        text = "No Maya sessions detected.";
    else
        for (auto& s : sessions)
        {
            text += s.title;
            text += s.frozen ? "  [FROZEN]\n" : "  [OK]\n";
        }

    MessageBoxA(NULL, text.c_str(),
        "Maya Sessions",
        MB_OK);
}

//////////////////////////////////////////////////////////////
// Window Proc
//////////////////////////////////////////////////////////////
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (l == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenu(menu, MF_STRING, 1, "Show Sessions");
            AppendMenu(menu, MF_STRING, 2, "Scan Now");
            AppendMenu(menu, MF_STRING, 3, "Crash Frozen Maya");
            AppendMenu(menu, MF_SEPARATOR, 0, NULL);
            AppendMenu(menu, MF_STRING, 4, "Exit");

            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(
                menu,
                TPM_RETURNCMD,
                p.x, p.y,
                0, hwnd, NULL);

            if (cmd == 1) ShowSessions();
            if (cmd == 2) { ScanMaya(); UpdateTrayIcon(); }
            if (cmd == 3) CrashFrozen();
            if (cmd == 4) PostQuitMessage(0);
        }
        break;
    }

    return DefWindowProc(hwnd, msg, w, l);
}

//////////////////////////////////////////////////////////////
// Entry
//////////////////////////////////////////////////////////////
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "MayaWatchdog";

    RegisterClass(&wc);

    g_hwnd = CreateWindow(
        wc.lpszClassName,
        "",
        0,0,0,0,0,
        NULL,NULL,hInst,NULL);

    nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    strcpy(nid.szTip, "Maya Manager");

    Shell_NotifyIcon(NIM_ADD, &nid);

    MSG msg;
    while (GetMessage(&msg,NULL,0,0))
    {
        ScanMaya();
        UpdateTrayIcon();
        Sleep(3000);

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    return 0;
}

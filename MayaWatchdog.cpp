#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")

#define WM_TRAYICON (WM_USER + 1)

struct MayaInstance
{
    HWND hwnd;
    DWORD pid;
    int hangSeconds;
};

std::vector<MayaInstance> g_mayas;
HWND g_hwnd;
NOTIFYICONDATA nid{};

// ------------------------------------------------
// SIMPLE LOGGER
// ------------------------------------------------
void Log(const std::string& msg)
{
    std::ofstream f("MayaWatchdog.log", std::ios::app);
    f << msg << "\n";
}

// ------------------------------------------------
// CHECK IF WINDOW BELONGS TO MAYA
// ------------------------------------------------
bool IsMayaProcess(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do {
            if (pe.th32ProcessID == pid)
            {
                std::string name = pe.szExeFile;
                CloseHandle(snap);

                return name.find("maya") != std::string::npos;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return false;
}

// ------------------------------------------------
// ENUM WINDOWS → FIND MAYA WINDOWS
// ------------------------------------------------
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (!IsWindowVisible(hwnd))
        return TRUE;

    if (IsMayaProcess(pid))
    {
        MayaInstance m{};
        m.hwnd = hwnd;
        m.pid = pid;
        m.hangSeconds = 0;

        g_mayas.push_back(m);
    }

    return TRUE;
}

void ScanMaya()
{
    g_mayas.clear();
    EnumWindows(EnumWindowsProc, 0);
}

// ------------------------------------------------
// HANG TEST
// ------------------------------------------------
bool IsHung(HWND hwnd)
{
    DWORD_PTR result;

    return !SendMessageTimeout(
        hwnd,
        WM_NULL,
        0,
        0,
        SMTO_ABORTIFHUNG,
        1500,
        &result);
}

// ------------------------------------------------
// AUTOSAVE VIA COMMANDPORT
// ------------------------------------------------
void AttemptAutoSave()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7002);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0)
    {
        const char* cmd =
            "import maya.cmds as cmds;"
            "cmds.file(save=True, force=True)\n";

        send(s, cmd, (int)strlen(cmd), 0);
        Log("Autosave command sent.");
    }

    closesocket(s);
}

// ------------------------------------------------
// RECOVERY LADDER
// ------------------------------------------------
void Recover(MayaInstance& maya)
{
    Log("Attempt recovery.");

    AttemptAutoSave();
    Sleep(3000);

    SendMessageTimeout(
        maya.hwnd,
        WM_CLOSE,
        0,
        0,
        SMTO_ABORTIFHUNG,
        3000,
        NULL);

    Sleep(4000);

    if (IsHung(maya.hwnd))
    {
        Log("Force terminating Maya.");

        HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, maya.pid);
        if (p)
        {
            TerminateProcess(p, 1);
            CloseHandle(p);
        }
    }
}

// ------------------------------------------------
// WATCHDOG LOOP
// ------------------------------------------------
void Monitor()
{
    ScanMaya();

    for (auto& m : g_mayas)
    {
        if (IsHung(m.hwnd))
        {
            m.hangSeconds += 2;

            if (m.hangSeconds >= 8)
            {
                Log("Freeze detected.");
                Recover(m);
            }
        }
    }
}

// ------------------------------------------------
// TRAY ICON
// ------------------------------------------------
void AddTrayIcon(HWND hwnd)
{
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    strcpy_s(nid.szTip, "Maya Watchdog");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

// ------------------------------------------------
// WINDOW PROC
// ------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
    WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        SetTimer(hwnd, 1, 2000, NULL);
        break;

    case WM_TIMER:
        Monitor();
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ------------------------------------------------
// ENTRY POINT
// ------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst,
    HINSTANCE, LPSTR, int)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "MayaWatchdogClass";

    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "",
        WS_OVERLAPPEDWINDOW,
        0,0,0,0,
        NULL,
        NULL,
        hInst,
        NULL);

    ShowWindow(g_hwnd, SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WSACleanup();
    return 0;
}

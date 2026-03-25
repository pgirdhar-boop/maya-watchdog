#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>

#define ID_TRAYICON 1001
#define WM_TRAYICON (WM_USER + 1)

#define ID_SCAN     2001
#define ID_CRASH    2002
#define ID_EXIT     2003

NOTIFYICONDATA nid{};
HWND g_hwnd = NULL;

//////////////////////////////////////////////////////////////
// Detect maya.exe
//////////////////////////////////////////////////////////////

bool IsMaya(const char* exe)
{
    return _stricmp(exe, "maya.exe") == 0;
}

std::vector<DWORD> GetMayaPIDs()
{
    std::vector<DWORD> result;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe))
    {
        do
        {
            if (IsMaya(pe.szExeFile))
                result.push_back(pe.th32ProcessID);

        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}

//////////////////////////////////////////////////////////////
// SAFE CRASH (CREATES MAYA RECOVERY FILE)
//////////////////////////////////////////////////////////////

void CrashMaya(DWORD pid)
{
    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE,
        FALSE,
        pid);

    if (!process)
        return;

    // Access violation stub (forces Maya crash handler)
    unsigned char crashCode[] =
    {
        0x48,0x31,0xC0, // xor rax,rax
        0x48,0x8B,0x00, // mov rax,[rax]
        0xC3            // ret
    };

    LPVOID remote =
        VirtualAllocEx(process,
            NULL,
            sizeof(crashCode),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);

    if (!remote)
    {
        CloseHandle(process);
        return;
    }

    WriteProcessMemory(
        process,
        remote,
        crashCode,
        sizeof(crashCode),
        NULL);

    HANDLE thread =
        CreateRemoteThread(
            process,
            NULL,
            0,
            (LPTHREAD_START_ROUTINE)remote,
            NULL,
            0,
            NULL);

    if (thread)
        CloseHandle(thread);

    CloseHandle(process);
}

//////////////////////////////////////////////////////////////
// UI HELPERS
//////////////////////////////////////////////////////////////

void ShowMessage(const char* text)
{
    MessageBoxA(NULL, text, "Maya Watchdog", MB_OK | MB_ICONINFORMATION);
}

void ScanMaya()
{
    auto pids = GetMayaPIDs();

    if (pids.empty())
    {
        ShowMessage("No running Maya sessions found.");
        return;
    }

    std::string msg = "Running Maya Sessions:\n\n";

    for (DWORD pid : pids)
        msg += "PID: " + std::to_string(pid) + "\n";

    MessageBoxA(NULL, msg.c_str(), "Maya Sessions", MB_OK);
}

void ConfirmCrash()
{
    auto pids = GetMayaPIDs();

    if (pids.empty())
    {
        ShowMessage("No Maya sessions running.");
        return;
    }

    for (DWORD pid : pids)
    {
        std::string msg =
            "Crash Maya PID " + std::to_string(pid) +
            "?\n\n(A recovery file will be created)";

        int res = MessageBoxA(
            NULL,
            msg.c_str(),
            "Confirm Crash",
            MB_YESNO | MB_ICONWARNING);

        if (res == IDYES)
            CrashMaya(pid);
    }
}

//////////////////////////////////////////////////////////////
// TRAY MENU
//////////////////////////////////////////////////////////////

void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();

    InsertMenu(menu, -1, MF_BYPOSITION, ID_SCAN,  "Scan Maya Sessions");
    InsertMenu(menu, -1, MF_BYPOSITION, ID_CRASH, "Crash Frozen Maya");
    InsertMenu(menu, -1, MF_SEPARATOR, 0, NULL);
    InsertMenu(menu, -1, MF_BYPOSITION, ID_EXIT,  "Exit");

    SetForegroundWindow(g_hwnd);

    TrackPopupMenu(
        menu,
        TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x,
        pt.y,
        0,
        g_hwnd,
        NULL);

    DestroyMenu(menu);
}

//////////////////////////////////////////////////////////////
// WINDOW PROC
//////////////////////////////////////////////////////////////

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_SCAN:
            ScanMaya();
            break;

        case ID_CRASH:
            ConfirmCrash();
            break;

        case ID_EXIT:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
            ShowTrayMenu();
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

//////////////////////////////////////////////////////////////
// ENTRY
//////////////////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "MayaWatchdog";

    RegisterClassA(&wc);

    g_hwnd = CreateWindowA(
        "MayaWatchdog",
        "",
        0,
        0,0,0,0,
        NULL,NULL,hInst,NULL);

    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = ID_TRAYICON;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    strcpy_s(nid.szTip, "Maya Watchdog");

    Shell_NotifyIcon(NIM_ADD, &nid);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

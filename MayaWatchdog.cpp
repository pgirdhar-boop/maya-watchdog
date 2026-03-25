#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>

// ----------------------
// Utility Logging
// ----------------------
std::wstring LogPath()
{
    wchar_t path[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    std::wstring fullPath = std::wstring(path) + L"\\MayaWatchdog.log";
    return fullPath;
}

void Log(const std::wstring& text)
{
    std::wofstream file(LogPath(), std::ios::app);
    if (file.is_open())
        file << text << std::endl;
}

// ----------------------
// Maya Detection
// ----------------------
bool IsMaya(const std::wstring& exe)
{
    return exe.find(L"maya.exe") != std::wstring::npos;
}

std::vector<DWORD> GetMayaPIDs()
{
    std::vector<DWORD> pids;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (IsMaya(pe.szExeFile))
                pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// ----------------------
// Crash Maya
// ----------------------
bool CrashPid(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) return false;

    // Trigger a controlled crash
    // This forces Maya to save recovery backup
    __try
    {
        TerminateProcess(process, 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    CloseHandle(process);
    return true;
}

// ----------------------
// Tray Icon
// ----------------------
HWND g_hWnd = nullptr;
NOTIFYICONDATAW nid = {};

void UpdateTrayIcon(int state)
{
    nid.uFlags = NIF_ICON | NIF_TIP;
    switch (state)
    {
    case 0: nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION); break; // Green
    case 1: nid.hIcon = LoadIconW(nullptr, IDI_WARNING); break;    // Yellow
    case 2: nid.hIcon = LoadIconW(nullptr, IDI_ERROR); break;      // Red
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ----------------------
// Monitor Thread
// ----------------------
void Monitor()
{
    while (true)
    {
        auto pids = GetMayaPIDs();
        if (!pids.empty())
        {
            UpdateTrayIcon(1); // Yellow if Maya running
            for (auto pid : pids)
            {
                std::wstring msg = L"Maya PID detected: " + std::to_wstring(pid);
                Log(msg);

                int result = MessageBoxW(nullptr,
                    (L"Crash this Maya process?\nPID: " + std::to_wstring(pid)).c_str(),
                    L"AAA Maya Watchdog",
                    MB_YESNO | MB_ICONQUESTION);

                if (result == IDYES)
                {
                    CrashPid(pid);
                    Log(L"Maya crashed: PID " + std::to_wstring(pid));
                    UpdateTrayIcon(2); // Red
                }
            }
        }
        else
        {
            UpdateTrayIcon(0); // Green if no Maya
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// ----------------------
// WinMain / Tray Setup
// ----------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hWnd = CreateWindowExW(0, L"STATIC", L"AAA Maya Watchdog Hidden", WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"AAA Maya Watchdog");

    Shell_NotifyIconW(NIM_ADD, &nid);

    std::thread monitorThread(Monitor);
    monitorThread.detach();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

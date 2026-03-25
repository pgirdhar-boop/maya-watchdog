// MayaWatchdog.cpp
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

NOTIFYICONDATAW nid;
HWND hwndMain;

// Logging utility
std::wstring LogPath()
{
    wchar_t path[MAX_PATH];
    GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    std::wstring logfile = std::wstring(path) + L"\\MayaWatchdog.log";
    return logfile;
}

void Log(const std::wstring& msg)
{
    std::wofstream file(LogPath().c_str(), std::ios::app);
    if (file.is_open())
    {
        file << msg << std::endl;
    }
}

// Check if executable is Maya
bool IsMaya(const std::wstring& exe)
{
    std::wstring lower = exe;
    for (auto &c : lower) c = towlower(c);
    return lower.find(L"maya.exe") != std::wstring::npos;
}

// Enumerate Maya PIDs
std::vector<DWORD> GetMayaPIDs()
{
    std::vector<DWORD> pids;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot)
    {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe))
        {
            do {
                if (IsMaya(pe.szExeFile))
                    pids.push_back(pe.th32ProcessID);
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return pids;
}

// Crash Maya safely with SEH
bool CrashPid(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) return false;

    __try
    {
        // Attempt to write invalid memory to trigger crash
        int crash = 0;
        WriteProcessMemory(process, (LPVOID)0x12345678, &crash, sizeof(crash), nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CloseHandle(process);
        return true;
    }
    CloseHandle(process);
    return false;
}

// Update tray icon based on state
void UpdateTrayIcon(int state)
{
    nid.uFlags = NIF_ICON | NIF_TIP;
    switch (state)
    {
    case 0: nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION); break; // Green
    case 1: nid.hIcon = LoadIconW(nullptr, IDI_WARNING); break;     // Yellow
    case 2: nid.hIcon = LoadIconW(nullptr, IDI_ERROR); break;       // Red
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// Simple monitoring loop
void Monitor()
{
    while (true)
    {
        auto mayaPIDs = GetMayaPIDs();
        if (mayaPIDs.empty())
        {
            UpdateTrayIcon(0); // Green - no Maya running
        }
        else
        {
            UpdateTrayIcon(1); // Yellow - Maya running
            for (auto pid : mayaPIDs)
            {
                // Could add dialog to ask which Maya to crash
                // For demo, just log
                Log(L"Detected Maya PID: " + std::to_wstring(pid));
            }
        }
        Sleep(2000);
    }
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    hwndMain = CreateWindowExW(0, L"STATIC", L"MayaAAAWatchdog", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, nullptr, nullptr, hInstance, nullptr);

    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"AAA Maya Watchdog");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Run monitor
    Monitor();

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>  // for std::max
#include <thread>
#include <chrono>
#include <mutex>

struct MayaSession {
    DWORD pid;
    std::wstring exeName;
    std::wstring sceneFile;
    bool isStuck;
};

std::mutex g_mutex;
std::vector<MayaSession> g_sessions;

HWND g_hWnd = nullptr;
NOTIFYICONDATA nid = {};

std::wstring GetMayaSceneFile(DWORD pid) {
    // Attempt to find currently opened scene (simplified)
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"C:\\Temp\\Maya_%u.ma", pid);
    return path;
}

bool IsMayaProcess(const char* exe) {
    return _stricmp(exe, "maya.exe") == 0;
}

std::vector<MayaSession> ScanMayaSessions() {
    std::vector<MayaSession> result;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(hSnap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if(!Process32First(hSnap, &pe)) {
        CloseHandle(hSnap);
        return result;
    }

    do {
        if(IsMayaProcess(pe.szExeFile)) {
            MayaSession s;
            s.pid = pe.th32ProcessID;
            s.exeName = L"maya.exe";
            s.sceneFile = GetMayaSceneFile(s.pid);
            s.isStuck = false; // initial assumption
            result.push_back(s);
        }
    } while(Process32Next(hSnap, &pe));

    CloseHandle(hSnap);
    return result;
}

void Log(const std::wstring& msg) {
    std::wofstream file("MayaWatchdog.log", std::ios::app);
    file << msg << std::endl;
}

bool CrashMaya(DWORD pid) {
    // Ask user confirmation before crash
    wchar_t msg[512];
    swprintf(msg, 512, L"Do you want to crash Maya PID %u?", pid);
    int res = MessageBoxW(nullptr, msg, L"Maya Watchdog", MB_YESNO | MB_ICONWARNING);
    if(res != IDYES) return false;

    // Trigger crash using invalid memory write
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if(!proc) return false;

    DWORD old;
    VirtualProtectEx(proc, (LPVOID)0x1, 1, PAGE_EXECUTE_READWRITE, &old);
    SIZE_T n;
    WriteProcessMemory(proc, (LPVOID)0x1, "\0", 1, &n); // crash Maya
    CloseHandle(proc);

    return true;
}

void UpdateTrayIcon(int state) {
    nid.uFlags = NIF_ICON | NIF_TIP;
    switch(state) {
        case 0: nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); break; // Green
        case 1: nid.hIcon = LoadIcon(nullptr, IDI_WARNING); break;    // Yellow
        case 2: nid.hIcon = LoadIcon(nullptr, IDI_ERROR); break;      // Red
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Monitor() {
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto sessions = ScanMayaSessions();
        int trayState = 0;
        for(auto& s : sessions) {
            // simplistic stuck detection: check CPU time, etc. (placeholder)
            s.isStuck = false; // Implement real stuck detection here
            trayState = std::max(trayState, s.isStuck ? 2 : 1);
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_sessions = sessions;
        }

        UpdateTrayIcon(trayState);
    }
}

void ShowSessionsWindow() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::wstring msg = L"Running Maya Sessions:\n";
    for(auto& s : g_sessions) {
        msg += L"PID: " + std::to_wstring(s.pid) + L" | Scene: " + s.sceneFile + (s.isStuck ? L" [Stuck]" : L"") + L"\n";
    }
    MessageBoxW(nullptr, msg.c_str(), L"Maya Sessions", MB_OK);
}

void TrayMenu() {
    POINT p;
    GetCursorPos(&p);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"Show Sessions");
    AppendMenuW(hMenu, MF_STRING, 2, L"Crash Frozen Maya");
    AppendMenuW(hMenu, MF_STRING, 3, L"Scan Now");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);

    switch(cmd) {
        case 1: ShowSessionsWindow(); break;
        case 2:
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                for(auto& s : g_sessions) {
                    if(s.isStuck) CrashMaya(s.pid);
                }
            }
            break;
        case 3: g_sessions = ScanMayaSessions(); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if(msg == WM_APP+1) {
        if(lParam == WM_RBUTTONUP) TrayMenu();
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MayaAAAWatchdog";
    RegisterClassW(&wc);

    g_hWnd = CreateWindowExW(0, L"MayaAAAWatchdog", L"AAA Maya Watchdog",
        0, 0,0,0,0, nullptr, nullptr, hInstance, nullptr);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP+1;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, 128, L"AAA Maya Watchdog");
    Shell_NotifyIconW(NIM_ADD, &nid);

    std::thread monitorThread(Monitor);
    monitorThread.detach();

    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

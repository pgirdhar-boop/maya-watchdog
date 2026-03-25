#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")

// ------------------------------------------------
// CONFIG
// ------------------------------------------------

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SCAN 1002
#define ID_TRAY_CRASH 1003
#define ID_TRAY_SHOW 1004

const int CHECK_INTERVAL = 5000;
const int STARTUP_GRACE = 60;

// ------------------------------------------------

struct MayaSession
{
    DWORD pid;
    HWND hwnd;
    std::string title;
    DWORD startTick;
    bool frozen=false;
};

std::map<DWORD,MayaSession> sessions;

HINSTANCE gInst;
HWND gWnd;
NOTIFYICONDATA nid;

// ------------------------------------------------
// CRASH THREAD (REAL CRASH = RECOVERY FILE)
// ------------------------------------------------

DWORD WINAPI CrashThread(LPVOID)
{
    volatile int* p=nullptr;
    *p=1;
    return 0;
}

bool CrashPid(DWORD pid)
{
    HANDLE h=OpenProcess(
        PROCESS_CREATE_THREAD,
        FALSE,pid);

    if(!h) return false;

    HANDLE t=CreateRemoteThread(
        h,nullptr,0,
        CrashThread,nullptr,0,nullptr);

    if(t) CloseHandle(t);
    CloseHandle(h);
    return true;
}

// ------------------------------------------------
// ENUM WINDOWS
// ------------------------------------------------

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM)
{
    char title[512];
    GetWindowTextA(hwnd,title,512);

    if(strstr(title,"Autodesk Maya"))
    {
        DWORD pid;
        GetWindowThreadProcessId(hwnd,&pid);

        MayaSession s;
        s.pid=pid;
        s.hwnd=hwnd;
        s.title=title;
        s.startTick=GetTickCount();

        sessions[pid]=s;
    }
    return TRUE;
}

// ------------------------------------------------

void ScanMaya()
{
    sessions.clear();
    EnumWindows(EnumProc,0);
}

// ------------------------------------------------

bool IsFrozen(MayaSession& s)
{
    DWORD alive=(GetTickCount()-s.startTick)/1000;

    if(alive<STARTUP_GRACE)
        return false;

    if(!IsWindow(s.hwnd))
        return false;

    return IsHungAppWindow(s.hwnd);
}

// ------------------------------------------------
// TRAY ICON STATE
// ------------------------------------------------

void SetTrayTip(const char* tip)
{
    strcpy_s(nid.szTip,tip);
    Shell_NotifyIcon(NIM_MODIFY,&nid);
}

// ------------------------------------------------
// WATCHDOG THREAD
// ------------------------------------------------

DWORD WINAPI Watchdog(LPVOID)
{
    while(true)
    {
        ScanMaya();

        bool frozenFound=false;

        for(auto& it:sessions)
        {
            auto& s=it.second;

            s.frozen=IsFrozen(s);

            if(s.frozen)
                frozenFound=true;
        }

        if(frozenFound)
            SetTrayTip("Maya Manager - Frozen detected");
        else
            SetTrayTip("Maya Manager - Running");

        Sleep(CHECK_INTERVAL);
    }
}

// ------------------------------------------------
// SHOW SESSION LIST
// ------------------------------------------------

void ShowSessions()
{
    std::string msg="Active Maya Sessions:\n\n";

    for(auto& it:sessions)
    {
        auto& s=it.second;

        msg+=std::to_string(s.pid)+" | ";
        msg+=s.title;
        msg+=s.frozen?" | FROZEN\n":" | OK\n";
    }

    MessageBoxA(NULL,msg.c_str(),
        "Maya Sessions",MB_OK);
}

// ------------------------------------------------
// CRASH FROZEN
// ------------------------------------------------

void CrashFrozen()
{
    for(auto& it:sessions)
    {
        if(it.second.frozen)
            CrashPid(it.second.pid);
    }
}

// ------------------------------------------------
// TRAY MENU
// ------------------------------------------------

void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu=CreatePopupMenu();

    InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,
        ID_TRAY_SHOW,"Show Sessions");

    InsertMenu(menu,1,MF_BYPOSITION|MF_STRING,
        ID_TRAY_CRASH,"Crash Frozen Maya");

    InsertMenu(menu,2,MF_BYPOSITION|MF_STRING,
        ID_TRAY_SCAN,"Scan Now");

    InsertMenu(menu,3,MF_BYPOSITION|MF_SEPARATOR,0,NULL);

    InsertMenu(menu,4,MF_BYPOSITION|MF_STRING,
        ID_TRAY_EXIT,"Exit");

    SetForegroundWindow(gWnd);

    TrackPopupMenu(menu,
        TPM_BOTTOMALIGN|TPM_LEFTALIGN,
        pt.x,pt.y,0,gWnd,NULL);

    DestroyMenu(menu);
}

// ------------------------------------------------
// WINDOW PROC
// ------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd,
    UINT msg,WPARAM w,LPARAM l)
{
    switch(msg)
    {
    case WM_TRAYICON:
        if(l==WM_RBUTTONUP)
            ShowTrayMenu();
        break;

    case WM_COMMAND:
        switch(LOWORD(w))
        {
        case ID_TRAY_EXIT:
            Shell_NotifyIcon(NIM_DELETE,&nid);
            PostQuitMessage(0);
            break;

        case ID_TRAY_SHOW:
            ShowSessions();
            break;

        case ID_TRAY_CRASH:
            CrashFrozen();
            break;

        case ID_TRAY_SCAN:
            ScanMaya();
            break;
        }
        break;
    }

    return DefWindowProc(hwnd,msg,w,l);
}

// ------------------------------------------------
// MAIN
// ------------------------------------------------

int WINAPI WinMain(HINSTANCE hInst,
    HINSTANCE,LPSTR,int)
{
    gInst=hInst;

    WNDCLASS wc{};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;
    wc.lpszClassName="MayaMgr";

    RegisterClass(&wc);

    gWnd=CreateWindow("MayaMgr","",
        WS_OVERLAPPEDWINDOW,
        0,0,0,0,
        NULL,NULL,hInst,NULL);

    nid.cbSize=sizeof(nid);
    nid.hWnd=gWnd;
    nid.uID=1;
    nid.uFlags=NIF_MESSAGE|NIF_TIP|NIF_ICON;
    nid.uCallbackMessage=WM_TRAYICON;
    nid.hIcon=LoadIcon(NULL,IDI_APPLICATION);

    strcpy_s(nid.szTip,"Maya Manager");

    Shell_NotifyIcon(NIM_ADD,&nid);

    CreateThread(NULL,0,Watchdog,NULL,0,NULL);

    MSG msg;
    while(GetMessage(&msg,NULL,0,0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

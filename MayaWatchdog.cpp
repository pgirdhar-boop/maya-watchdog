#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winsock2.h>
#include <fstream>
#include <vector>
#include <string>

#pragma comment(lib,"ws2_32.lib")

#define PORT 50555
#define WM_TRAYICON (WM_USER + 1)

NOTIFYICONDATA nid;

void Log(std::string msg)
{
    std::ofstream f("MayaManager_log.txt", std::ios::app);
    f << msg << "\n";
}

bool SendCommand(const char* cmd, std::string& response)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(s,(sockaddr*)&server,sizeof(server))!=0)
        return false;

    send(s,cmd,strlen(cmd),0);

    char buf[512]={0};
    int r = recv(s,buf,512,0);
    if(r>0) response = buf;

    closesocket(s);
    WSACleanup();
    return true;
}

std::vector<DWORD> FindMaya()
{
    std::vector<DWORD> pids;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32 pe{sizeof(pe)};

    Process32First(snap,&pe);

    do{
        if(!_stricmp(pe.szExeFile,"maya.exe"))
            pids.push_back(pe.th32ProcessID);
    }
    while(Process32Next(snap,&pe));

    CloseHandle(snap);
    return pids;
}

bool WindowAlive(HWND hwnd)
{
    DWORD_PTR result;
    return SendMessageTimeout(
        hwnd, WM_NULL,0,0,
        SMTO_ABORTIFHUNG,2000,&result) != 0;
}

HWND GetMainWindow(DWORD pid)
{
    HWND hwnd = GetTopWindow(NULL);
    while(hwnd)
    {
        DWORD wpid;
        GetWindowThreadProcessId(hwnd,&wpid);

        if(wpid==pid && IsWindowVisible(hwnd))
            return hwnd;

        hwnd = GetNextWindow(hwnd,GW_HWNDNEXT);
    }
    return NULL;
}

void ShowTray(HWND hwnd)
{
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    strcpy_s(nid.szTip,"Maya Manager Running");

    Shell_NotifyIcon(NIM_ADD,&nid);
}

void CheckMaya()
{
    auto pids = FindMaya();

    for(auto pid : pids)
    {
        HWND hwnd = GetMainWindow(pid);
        if(!hwnd) continue;

        if(!WindowAlive(hwnd))
        {
            std::string scene;
            if(!SendCommand("PING",scene))
                continue;

            std::string msg =
                "Maya not responding.\n\nScene:\n"
                + scene +
                "\n\nRecover session?";

            int r = MessageBoxA(
                NULL,
                msg.c_str(),
                "Maya Manager",
                MB_YESNO | MB_ICONWARNING
            );

            if(r==IDYES)
            {
                std::string resp;
                SendCommand("CRASH",resp);
                Log("Recovery triggered for " + scene);
            }
            else
            {
                Log("Artist chose wait.");
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if(msg==WM_TRAYICON && l==WM_RBUTTONUP)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenu(menu,MF_STRING,1,"Exit");

        POINT p;
        GetCursorPos(&p);
        SetForegroundWindow(hwnd);

        int cmd = TrackPopupMenu(menu,TPM_RETURNCMD,
                                 p.x,p.y,0,hwnd,NULL);

        if(cmd==1)
            PostQuitMessage(0);
    }

    return DefWindowProc(hwnd,msg,w,l);
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int)
{
    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "MayaManager";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("MayaManager","",
        0,0,0,0,0,NULL,NULL,hInst,NULL);

    ShowTray(hwnd);

    Sleep(90000); // startup grace

    MSG msg;
    while(true)
    {
        while(PeekMessage(&msg,NULL,0,0,PM_REMOVE))
        {
            if(msg.message==WM_QUIT)
                return 0;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        CheckMaya();
        Sleep(5000);
    }
}

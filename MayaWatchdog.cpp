#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <map>
#include <string>
#include <thread>
#include <chrono>

#pragma comment(lib,"ws2_32.lib")

#define WM_TRAY (WM_USER+1)

struct Session
{
    int port;
    time_t lastBeat;
    std::string scene;
};

std::map<int,Session> sessions;
HWND hwnd;
NOTIFYICONDATA nid;

////////////////////////////////////////////////////

void UpdateTray(int state)
{
    // 0 green,1 yellow,2 red
    nid.hIcon = LoadIcon(NULL,
        state==0?IDI_APPLICATION:
        state==1?IDI_WARNING:
                 IDI_ERROR);

    Shell_NotifyIcon(NIM_MODIFY,&nid);
}

////////////////////////////////////////////////////

std::string SendCmd(int port,const char* cmd)
{
    SOCKET s=socket(AF_INET,SOCK_STREAM,0);

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=inet_addr("127.0.0.1");

    if(connect(s,(sockaddr*)&addr,sizeof(addr))<0)
        return "";

    send(s,cmd,strlen(cmd),0);

    char buf[1024]={0};
    recv(s,buf,1024,0);

    closesocket(s);
    return buf;
}

////////////////////////////////////////////////////

void HeartbeatServer()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2,2),&w);

    SOCKET server=socket(AF_INET,SOCK_STREAM,0);

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(50500);
    addr.sin_addr.s_addr=INADDR_ANY;

    bind(server,(sockaddr*)&addr,sizeof(addr));
    listen(server,10);

    while(true)
    {
        SOCKET c=accept(server,NULL,NULL);

        char buf[64]={0};
        recv(c,buf,64,0);

        int port=atoi(buf);

        sessions[port].port=port;
        sessions[port].lastBeat=time(NULL);

        closesocket(c);
    }
}

////////////////////////////////////////////////////

void Monitor()
{
    while(true)
    {
        time_t now=time(NULL);

        int state=0;

        for(auto&[p,s]:sessions)
        {
            int diff=int(now-s.lastBeat);

            if(diff>12)
            {
                state=2;

                std::string scene=SendCmd(p,"PING");

                std::string msg=
                    "Frozen Maya detected:\n\n"+scene+
                    "\n\nRecover?";

                if(MessageBox(NULL,msg.c_str(),
                    "Studio Watchdog",
                    MB_YESNO|MB_ICONWARNING)==IDYES)
                {
                    std::string path=SendCmd(p,"SAVE");

                    MessageBox(NULL,
                        ("Saved:\n"+path).c_str(),
                        "Recovery Complete",
                        MB_OK);
                }
            }
            else if(diff>6)
                state=max(state,1);
        }

        UpdateTray(state);

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

////////////////////////////////////////////////////

LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_TRAY && l==WM_RBUTTONUP)
        PostQuitMessage(0);

    return DefWindowProc(h,m,w,l);
}

////////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int)
{
    WNDCLASS wc{};
    wc.lpfnWndProc=Proc;
    wc.lpszClassName="StudioWatchdog";
    RegisterClass(&wc);

    hwnd=CreateWindow("StudioWatchdog","",
        0,0,0,0,0,0,0,hInst,0);

    nid.cbSize=sizeof(nid);
    nid.hWnd=hwnd;
    nid.uID=1;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    nid.uCallbackMessage=WM_TRAY;
    nid.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    strcpy_s(nid.szTip,"Studio Maya Watchdog");

    Shell_NotifyIcon(NIM_ADD,&nid);

    std::thread(HeartbeatServer).detach();
    std::thread(Monitor).detach();

    MSG msg;
    while(GetMessage(&msg,NULL,0,0))
        DispatchMessage(&msg);

    return 0;
}

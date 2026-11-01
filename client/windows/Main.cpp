#include "Connection.h"
#include "irb_protocol.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <algorithm>
using json=nlohmann::json;
namespace {
constexpr UINT FrameMessage=WM_APP+1,StatusMessage=WM_APP+2,ResponseMessage=WM_APP+3;
HWND window,configEdit,textEdit,bitrateEdit,controlCheck,notice;
std::unique_ptr<Connection> connection;
std::mutex frameMutex,statusMutex;
VideoFrame frame;
std::wstring status;
std::string responseNotice;
std::atomic<int> rotation{0};
std::atomic<bool> framePosted{false};
int frameRotation=0,currentFPS=30;
double currentScale=1;
RECT videoRect{};
bool dragging=false;
double lastX=0,lastY=0;
enum {Config=10,Browse,Connect,Disconnect,Home,Switcher,VolumeUp,VolumeDown,SendText,Bitrate};
std::wstring wide(const std::string &text) {
    int size=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),size); return out;
}
std::string utf8(const std::wstring &text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr); return out;
}
std::wstring editText(HWND edit) {
    int size=GetWindowTextLengthW(edit); std::wstring out(static_cast<size_t>(size)+1,L'\0');
    GetWindowTextW(edit,out.data(),size+1); out.resize(static_cast<size_t>(size)); return out;
}
HWND control(const wchar_t *cls,const wchar_t *title,DWORD style,int x,int y,int w,int h,int id) {
    HWND c=CreateWindowW(cls,title,WS_VISIBLE|WS_CHILD|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE); return c;
}
VideoFrame rotate(VideoFrame input,int degrees) {
    if(degrees==0) return input;
    VideoFrame output;
    output.width=degrees==180?input.width:input.height; output.height=degrees==180?input.height:input.width;
    output.bgra.resize(input.bgra.size());
    for(int y=0;y<input.height;++y) for(int x=0;x<input.width;++x) {
        int dx,dy;
        if(degrees==90) { dx=input.height-1-y; dy=x; }
        else if(degrees==180) { dx=input.width-1-x; dy=input.height-1-y; }
        else { dx=y; dy=input.width-1-x; }
        std::copy_n(input.bgra.data()+(static_cast<size_t>(y)*input.width+x)*4,4,
                    output.bgra.data()+(static_cast<size_t>(dy)*output.width+dx)*4);
    } return output;
}
void command(json request) {
    if(!connection||!connection->command(std::move(request))) SetWindowTextW(notice,L"Connect and approve on the iPhone before sending commands.");
}
void endDrag() {
    if(dragging) { command({{"op","touch"},{"finger",0},{"phase",2},{"x",lastX},{"y",lastY}}); dragging=false; ReleaseCapture(); }
}
void disconnect() {
    endDrag(); if(connection) connection->close(); connection.reset();
    { std::lock_guard<std::mutex> lock(frameMutex); frame=VideoFrame{}; }
    InvalidateRect(window,nullptr,TRUE); SetWindowTextW(window,L"iOSRemoteBridge - disconnected");
}
void connectClient() {
    disconnect();
    try {
        std::ifstream stream{std::filesystem::path(editText(configEdit))};
        if(!stream) throw std::runtime_error("Select the local client-config.json pairing file");
        json config; stream>>config;
        bool wantsControl=SendMessageW(controlCheck,BM_GETCHECK,0,0)==BST_CHECKED;
        connection=std::make_unique<Connection>(config,wantsControl,currentFPS);
        connection->onFrame=[](VideoFrame &&source) {
            int r=rotation.load(); VideoFrame shown=rotate(std::move(source),r);
            { std::lock_guard<std::mutex> lock(frameMutex); frame=std::move(shown); frameRotation=r; }
            if(!framePosted.exchange(true)) PostMessageW(window,FrameMessage,0,0);
        };
        connection->onStatus=[](const std::string &message) {
            { std::lock_guard<std::mutex> lock(statusMutex); status=wide(message); }
            PostMessageW(window,StatusMessage,0,0);
        };
        connection->onResponse=[](const json &reply) {
            if(reply.contains("ok")&&!reply.value("ok",false)) {
                { std::lock_guard<std::mutex> lock(statusMutex); responseNotice=reply.value("error",std::string("operation failed")); }
                PostMessageW(window,ResponseMessage,0,0);
            }
        };
        SetWindowTextW(notice,L"Approve on the iPhone. Ctrl+T focuses text. Esc disconnects. F1 rotates the view.");
        connection->start();
    } catch(const std::exception &) { SetWindowTextW(notice,L"Cannot load pairing file or configuration. Check the local JSON file and required fields."); connection.reset(); }
}
void configure(int fps,double scale) {
    currentFPS=fps; currentScale=scale;
    int bitrate=GetDlgItemInt(window,Bitrate,nullptr,FALSE)*1000;
    command({{"op","configure"},{"fps",fps},{"scale",scale},{"bitrate",bitrate}});
    SetWindowTextW(notice,L"Quality requested. If a frame is outstanding, retry after the server response.");
}
bool point(LPARAM position,double &x,double &y) {
    double px=GET_X_LPARAM(position),py=GET_Y_LPARAM(position);
    if(videoRect.right<=videoRect.left||videoRect.bottom<=videoRect.top) return false;
    if(px<videoRect.left||px>videoRect.right||py<videoRect.top||py>videoRect.bottom) return false;
    int r; { std::lock_guard<std::mutex> lock(frameMutex); r=frameRotation; }
    return irb_unrotate((px-videoRect.left)/(videoRect.right-videoRect.left),
                        (py-videoRect.top)/(videoRect.bottom-videoRect.top),r,&x,&y)!=0;
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        window=hwnd;
        configEdit=control(L"EDIT",L"client-config.json",WS_BORDER|ES_AUTOHSCROLL,8,8,410,25,Config);
        control(L"BUTTON",L"Pairing file...",0,424,8,98,25,Browse);
        control(L"BUTTON",L"Connect",0,528,8,72,25,Connect);
        control(L"BUTTON",L"Disconnect",0,606,8,82,25,Disconnect);
        controlCheck=control(L"BUTTON",L"Request control",BS_AUTOCHECKBOX,700,8,135,25,0);
        SendMessageW(controlCheck,BM_SETCHECK,BST_CHECKED,0);
        control(L"BUTTON",L"Home",0,8,39,60,25,Home);
        control(L"BUTTON",L"App switcher",0,74,39,94,25,Switcher);
        control(L"BUTTON",L"Vol +",0,174,39,52,25,VolumeUp);
        control(L"BUTTON",L"Vol -",0,232,39,52,25,VolumeDown);
        textEdit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,292,39,230,25,0);
        SendMessageW(textEdit,EM_SETLIMITTEXT,1024,0);
        control(L"BUTTON",L"Send text",0,528,39,72,25,SendText);
        bitrateEdit=control(L"EDIT",L"8000",WS_BORDER|ES_NUMBER,606,39,60,25,Bitrate);
        control(L"STATIC",L"kbps (Enter)",0,672,43,95,20,0);
        notice=control(L"STATIC",L"Choose a pairing file generated by tools/provision.py. Read docs/DEVICE_TESTING.md before installing.",0,8,72,1000,22,0);
        return 0;
    case WM_COMMAND:
        switch(LOWORD(w)) {
        case Browse: {
            wchar_t path[MAX_PATH]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=hwnd;
            dialog.lpstrFilter=L"Pairing JSON\0*.json\0"; dialog.lpstrFile=path; dialog.nMaxFile=MAX_PATH; dialog.Flags=OFN_FILEMUSTEXIST;
            if(GetOpenFileNameW(&dialog)) SetWindowTextW(configEdit,path); return 0;
        }
        case Connect: connectClient(); return 0;
        case Disconnect: disconnect(); return 0;
        case Home: command({{"op","press_home"}}); return 0;
        case Switcher: command({{"op","app_switcher"}}); return 0;
        case VolumeUp: command({{"op","volume_up"}}); return 0;
        case VolumeDown: command({{"op","volume_down"}}); return 0;
        case SendText: command({{"op","type_text"},{"text",utf8(editText(textEdit))}}); return 0;
        } break;
    case WM_KEYDOWN:
        if(w==VK_ESCAPE) { disconnect(); return 0; }
        if(w==VK_F1) { endDrag(); rotation=(rotation.load()+90)%360; return 0; }
        if(w==VK_F2) { configure(30,currentScale); return 0; }
        if(w==VK_F3) { configure(60,currentScale); return 0; }
        if(w==VK_F4) { configure(currentFPS,.5); return 0; }
        if(w==VK_F5) { configure(currentFPS,1); return 0; }
        if(w==VK_RETURN&&GetFocus()==bitrateEdit) { command({{"op","configure"},{"bitrate",GetDlgItemInt(hwnd,Bitrate,nullptr,FALSE)*1000}}); return 0; }
        if((GetKeyState(VK_CONTROL)&0x8000)&&w=='T') { SetFocus(textEdit); return 0; }
        if(w>=VK_LEFT&&w<=VK_DOWN) {
            static const int usages[]={0x50,0x52,0x4f,0x51};
            command({{"op","key"},{"usage",usages[w-VK_LEFT]}}); return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        double x,y; SetFocus(hwnd);
        if(point(l,x,y)&&connection&&connection->authorized()) {
            lastX=x; lastY=y; dragging=true; SetCapture(hwnd);
            command({{"op","touch"},{"finger",0},{"phase",0},{"x",x},{"y",y}});
        } return 0;
    }
    case WM_MOUSEMOVE:
        if(dragging) { double x,y; if(point(l,x,y)) { lastX=x; lastY=y; command({{"op","touch"},{"finger",0},{"phase",1},{"x",x},{"y",y}}); } } return 0;
    case WM_LBUTTONUP: endDrag(); return 0;
    case WM_CAPTURECHANGED: if(dragging) endDrag(); return 0;
    case FrameMessage: framePosted=false; InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case StatusMessage: { std::lock_guard<std::mutex> lock(statusMutex); SetWindowTextW(hwnd,(L"iOSRemoteBridge | "+status).c_str()); return 0; }
    case ResponseMessage: { std::lock_guard<std::mutex> lock(statusMutex); SetWindowTextW(notice,wide(responseNotice).c_str()); return 0; }
    case WM_SIZE: InvalidateRect(hwnd,nullptr,TRUE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(hwnd,&paint); RECT area; GetClientRect(hwnd,&area); area.top=100;
        FillRect(dc,&area,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        std::lock_guard<std::mutex> lock(frameMutex);
        videoRect={};
        if(frame.width&&frame.height&&area.right>0&&area.bottom>area.top) {
            double scale=std::min(static_cast<double>(area.right)/frame.width,static_cast<double>(area.bottom-area.top)/frame.height);
            int width=static_cast<int>(frame.width*scale),height=static_cast<int>(frame.height*scale);
            int left=(area.right-width)/2,top=area.top+(area.bottom-area.top-height)/2;
            videoRect={left,top,left+width,top+height};
            BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bitmap.bmiHeader.biWidth=frame.width;
            bitmap.bmiHeader.biHeight=-frame.height; bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,left,top,width,height,0,0,frame.width,frame.height,frame.bgra.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
        } EndPaint(hwnd,&paint); return 0;
    }
    case WM_CLOSE: disconnect(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 1;
    SetProcessDPIAware();
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=procedure; cls.lpszClassName=L"iOSRemoteBridge.Window";
    cls.hCursor=LoadCursor(nullptr,IDC_ARROW); cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    RegisterClassW(&cls);
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"iOSRemoteBridge - pair and connect",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                           CW_USEDEFAULT,CW_USEDEFAULT,1100,950,nullptr,nullptr,instance,nullptr);
    if(!hwnd) { WSACleanup(); return 1; }
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv&&argc>1&&std::wstring(argv[1])==L"--self-test-ui") {
        bool controls=GetDlgItem(hwnd,Config)&&GetDlgItem(hwnd,Connect)&&GetDlgItem(hwnd,Disconnect)&&
                      GetDlgItem(hwnd,SendText)&&GetDlgItemInt(hwnd,Bitrate,nullptr,FALSE)==8000;
        LocalFree(argv); DestroyWindow(hwnd); WSACleanup(); return controls?0:1;
    }
    if(argv&&argc>1) SetWindowTextW(configEdit,argv[1]); if(argv) LocalFree(argv);
    ShowWindow(hwnd,show); MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        bool shortcut=message.message==WM_KEYDOWN&&(message.wParam==VK_ESCAPE||
                      (message.wParam>=VK_F1&&message.wParam<=VK_F5)||
                      ((GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='T')||
                      (message.wParam==VK_RETURN&&GetFocus()==bitrateEdit));
        if(shortcut) SendMessageW(hwnd,message.message,message.wParam,message.lParam);
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    connection.reset(); WSACleanup(); return 0;
}

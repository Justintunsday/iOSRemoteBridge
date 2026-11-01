#include "Connection.h"
#include "irb_protocol.h"
#include "RpcReply.h"
#include "BufferedRenderer.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cmath>
using json=nlohmann::json;
namespace {
constexpr UINT FrameMessage=WM_APP+1,StatusMessage=WM_APP+2,ResponseMessage=WM_APP+3;
constexpr int ToolbarHeight=170,MinimumClientWidth=380;
HWND window,configEdit,textEdit,bitrateEdit,controlCheck,notice;
HWND browseButton,connectButton,disconnectButton,homeButton,switcherButton,volumeUpButton,volumeDownButton;
HWND sendTextButton,bitrateLabel,rotateButton,fitButton,ratioCheck;
BufferedRenderer renderer;
std::unique_ptr<Connection> connection;
std::mutex frameMutex,statusMutex;
VideoFrame frame;
std::wstring status;
std::string responseNotice;
std::atomic<int> rotation{0};
std::atomic<bool> framePosted{false};
int frameRotation=0,currentFPS=60;
double currentScale=1;
RECT videoRect{};
bool dragging=false;
bool keepRatio=true,fitNextFrame=true;
int viewWidth=750,viewHeight=1334;
RECT sizingStart{};
double lastX=0,lastY=0;
ULONGLONG lastMove=0;
enum {Config=10,Browse,Connect,Disconnect,Home,Switcher,VolumeUp,VolumeDown,SendText,Bitrate,Rotate,Fit,KeepRatio};
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
void layoutControls(int width) {
    if(width<=0) return;
    auto place=[](HWND child,int x,int y,int w,int h=24) { MoveWindow(child,x,y,std::max(1,w),h,TRUE); };
    place(configEdit,8,6,width-106); place(browseButton,width-92,6,84);
    place(connectButton,8,36,66); place(disconnectButton,80,36,76);
    place(controlCheck,162,36,128); place(fitButton,296,36,54);
    place(homeButton,8,66,54); place(switcherButton,68,66,90);
    place(volumeUpButton,164,66,44); place(volumeDownButton,214,66,44); place(rotateButton,264,66,64);
    int textWidth=width-204;
    place(textEdit,8,96,textWidth); place(sendTextButton,14+textWidth,96,70);
    place(bitrateEdit,90+textWidth,96,60); place(bitrateLabel,156+textWidth,100,40,20);
    place(ratioCheck,8,126,126); place(notice,140,126,width-148,38);
}
SIZE windowBorder(HWND hwnd) {
    RECT rect{};
    AdjustWindowRectEx(&rect,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,
                      static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)));
    return {rect.right-rect.left,rect.bottom-rect.top};
}
RECT videoArea() { RECT area{}; GetClientRect(window,&area); area.top=std::min<long>(ToolbarHeight,area.bottom); return area; }
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
void fitWindow() {
    endDrag();
    if(IsZoomed(window)) ShowWindow(window,SW_RESTORE);
    MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) return;
    SIZE border=windowBorder(window);
    int availableWidth=std::max(1L,monitor.rcWork.right-monitor.rcWork.left-border.cx-32);
    int availableHeight=std::max(1L,monitor.rcWork.bottom-monitor.rcWork.top-border.cy-ToolbarHeight-32);
    double scale=std::min({1.0,static_cast<double>(availableWidth)/viewWidth,static_cast<double>(availableHeight)/viewHeight});
    int clientWidth=std::min(availableWidth,std::max(MinimumClientWidth,static_cast<int>(std::lround(viewWidth*scale))));
    int clientHeight=std::min(availableHeight,static_cast<int>(std::lround(viewHeight*scale)))+ToolbarHeight;
    int width=clientWidth+border.cx,height=clientHeight+border.cy;
    RECT current{}; GetWindowRect(window,&current);
    int left=std::clamp((current.left+current.right-width)/2,monitor.rcWork.left,monitor.rcWork.right-width);
    int top=std::clamp((current.top+current.bottom-height)/2,monitor.rcWork.top,monitor.rcWork.bottom-height);
    SetWindowPos(window,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
void constrainSize(HWND hwnd,UINT edge,RECT &rect) {
    SIZE border=windowBorder(hwnd);
    int width=std::max(MinimumClientWidth,static_cast<int>(rect.right-rect.left-border.cx));
    int height=std::max(1,static_cast<int>(rect.bottom-rect.top-border.cy-ToolbarHeight));
    bool fromWidth=edge==WMSZ_LEFT||edge==WMSZ_RIGHT;
    if(edge!=WMSZ_TOP&&edge!=WMSZ_BOTTOM&&!fromWidth) {
        double dx=std::abs((rect.right-rect.left)-(sizingStart.right-sizingStart.left));
        double dy=std::abs((rect.bottom-rect.top)-(sizingStart.bottom-sizingStart.top));
        fromWidth=dx>=dy*viewWidth/viewHeight;
    }
    if(fromWidth) height=std::max(1,static_cast<int>(std::lround(static_cast<double>(width)*viewHeight/viewWidth)));
    else {
        width=std::max(MinimumClientWidth,static_cast<int>(std::lround(static_cast<double>(height)*viewWidth/viewHeight)));
        height=std::max(1,static_cast<int>(std::lround(static_cast<double>(width)*viewHeight/viewWidth)));
    }
    width+=border.cx; height+=border.cy+ToolbarHeight;
    if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT) rect.left=rect.right-width;
    else rect.right=rect.left+width;
    if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT) rect.top=rect.bottom-height;
    else rect.bottom=rect.top+height;
}
void disconnect() {
    endDrag(); if(connection) connection->close(); connection.reset();
    { std::lock_guard<std::mutex> lock(frameMutex); frame=VideoFrame{}; }
    InvalidateRect(window,nullptr,FALSE); SetWindowTextW(window,L"iOSRemoteBridge - disconnected");
}
void connectClient() {
    disconnect();
    fitNextFrame=true;
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
            if(reply.contains("ok")&&!responseSucceeded(reply)) {
                { std::lock_guard<std::mutex> lock(statusMutex); responseNotice=reply.value("error",std::string("operation failed")); }
                PostMessageW(window,ResponseMessage,0,0);
            }
        };
        SetWindowTextW(notice,L"Approve on the iPhone. Esc disconnects.");
        connection->start();
    } catch(const std::exception &) { SetWindowTextW(notice,L"Cannot load pairing file or configuration. Check the local JSON file and required fields."); connection.reset(); }
}
void configure(int fps,double scale) {
    currentFPS=fps; currentScale=scale;
    int bitrate=GetDlgItemInt(window,Bitrate,nullptr,FALSE)*1000;
    command({{"op","configure"},{"fps",fps},{"scale",scale},{"bitrate",bitrate}});
    SetWindowTextW(notice,L"Updating video quality...");
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
        browseButton=control(L"BUTTON",L"Pairing file...",0,424,8,98,25,Browse);
        connectButton=control(L"BUTTON",L"Connect",0,528,8,72,25,Connect);
        disconnectButton=control(L"BUTTON",L"Disconnect",0,606,8,82,25,Disconnect);
        controlCheck=control(L"BUTTON",L"Request control",BS_AUTOCHECKBOX,700,8,135,25,0);
        SendMessageW(controlCheck,BM_SETCHECK,BST_CHECKED,0);
        homeButton=control(L"BUTTON",L"Home",0,8,39,60,25,Home);
        switcherButton=control(L"BUTTON",L"App switcher",0,74,39,94,25,Switcher);
        volumeUpButton=control(L"BUTTON",L"Vol +",0,174,39,52,25,VolumeUp);
        volumeDownButton=control(L"BUTTON",L"Vol -",0,232,39,52,25,VolumeDown);
        textEdit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,292,39,230,25,0);
        SendMessageW(textEdit,EM_SETLIMITTEXT,1024,0);
        sendTextButton=control(L"BUTTON",L"Send text",0,528,39,72,25,SendText);
        bitrateEdit=control(L"EDIT",L"8000",WS_BORDER|ES_NUMBER,606,39,60,25,Bitrate);
        bitrateLabel=control(L"STATIC",L"kbps",0,672,43,95,20,0);
        rotateButton=control(L"BUTTON",L"Rotate",0,0,0,64,24,Rotate);
        fitButton=control(L"BUTTON",L"Fit",0,0,0,54,24,Fit);
        ratioCheck=control(L"BUTTON",L"Keep ratio",BS_AUTOCHECKBOX,0,0,126,24,KeepRatio);
        SendMessageW(ratioCheck,BM_SETCHECK,BST_CHECKED,0);
        notice=control(L"STATIC",L"Choose a pairing file, then connect.",SS_NOPREFIX,8,72,1000,22,0);
        { RECT client{}; GetClientRect(hwnd,&client); layoutControls(client.right); }
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
        case Rotate: endDrag(); rotation=(rotation.load()+90)%360; return 0;
        case Fit: fitWindow(); return 0;
        case KeepRatio:
            keepRatio=SendMessageW(ratioCheck,BM_GETCHECK,0,0)==BST_CHECKED;
            if(keepRatio) fitWindow(); return 0;
        } break;
    case WM_KEYDOWN:
        if(w==VK_ESCAPE) { disconnect(); return 0; }
        if(w==VK_F1) { endDrag(); rotation=(rotation.load()+90)%360; return 0; }
        if(w==VK_F2) { configure(30,currentScale); return 0; }
        if(w==VK_F3) { configure(60,currentScale); return 0; }
        if(w==VK_F4) { configure(currentFPS,.5); return 0; }
        if(w==VK_F5) { configure(currentFPS,1); return 0; }
        if(w==VK_F6) { fitWindow(); return 0; }
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
        if(dragging) { double x,y; if(point(l,x,y)) {
            lastX=x; lastY=y; ULONGLONG now=GetTickCount64();
            if(now-lastMove>=8) { lastMove=now; command({{"op","touch"},{"finger",0},{"phase",1},{"x",x},{"y",y}}); }
        } } return 0;
    case WM_LBUTTONUP: endDrag(); return 0;
    case WM_CAPTURECHANGED: if(dragging) endDrag(); return 0;
    case FrameMessage: {
        framePosted=false;
        int width,height;
        { std::lock_guard<std::mutex> lock(frameMutex); width=frame.width; height=frame.height; }
        if(width>0&&height>0) {
            bool changed=static_cast<int64_t>(width)*viewHeight!=static_cast<int64_t>(height)*viewWidth;
            viewWidth=width; viewHeight=height;
            bool fit=keepRatio&&(fitNextFrame||changed); fitNextFrame=false;
            if(fit&&!IsZoomed(hwnd)&&!IsIconic(hwnd)) fitWindow();
        }
        RECT area=videoArea(); InvalidateRect(hwnd,&area,FALSE); return 0;
    }
    case StatusMessage: { std::lock_guard<std::mutex> lock(statusMutex); SetWindowTextW(hwnd,(L"iOSRemoteBridge | "+status).c_str()); return 0; }
    case ResponseMessage: { std::lock_guard<std::mutex> lock(statusMutex); SetWindowTextW(notice,wide(responseNotice).c_str()); return 0; }
    case WM_ENTERSIZEMOVE: endDrag(); GetWindowRect(hwnd,&sizingStart); return 0;
    case WM_SIZING:
        if(keepRatio) { constrainSize(hwnd,static_cast<UINT>(w),*reinterpret_cast<RECT *>(l)); return TRUE; }
        break;
    case WM_GETMINMAXINFO: {
        SIZE border=windowBorder(hwnd); auto *limits=reinterpret_cast<MINMAXINFO *>(l);
        limits->ptMinTrackSize.x=MinimumClientWidth+border.cx;
        limits->ptMinTrackSize.y=ToolbarHeight+120+border.cy; return 0;
    }
    case WM_SIZE:
        if(w!=SIZE_MINIMIZED) { layoutControls(LOWORD(l)); videoRect={}; InvalidateRect(hwnd,nullptr,FALSE); }
        return 0;
    case WM_ERASEBKGND: return 1; // Buffered WM_PAINT owns the entire client background.
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(hwnd,&paint); RECT area; GetClientRect(hwnd,&area);
        std::lock_guard<std::mutex> lock(frameMutex);
        renderer.paint(dc,area.right,area.bottom,ToolbarHeight,frame,videoRect);
        EndPaint(hwnd,&paint); return 0;
    }
    case WM_CLOSE: disconnect(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY: renderer.reset(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 1;
    SetProcessDPIAware();
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=procedure; cls.lpszClassName=L"iOSRemoteBridge.Window";
    cls.hCursor=LoadCursor(nullptr,IDC_ARROW); cls.hbrBackground=nullptr;
    RegisterClassW(&cls);
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"iOSRemoteBridge - pair and connect",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                           CW_USEDEFAULT,CW_USEDEFAULT,460,950,nullptr,nullptr,instance,nullptr);
    if(!hwnd) { WSACleanup(); return 1; }
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv&&argc>1&&std::wstring(argv[1])==L"--self-test-ui") {
        bool controls=GetDlgItem(hwnd,Config)&&GetDlgItem(hwnd,Connect)&&GetDlgItem(hwnd,Disconnect)&&
                      GetDlgItem(hwnd,SendText)&&GetDlgItem(hwnd,Fit)&&GetDlgItem(hwnd,Rotate)&&
                      SendMessageW(ratioCheck,BM_GETCHECK,0,0)==BST_CHECKED&&
                      GetDlgItemInt(hwnd,Bitrate,nullptr,FALSE)==8000;
        for(int r=0;r<2;++r) {
            viewWidth=r?1334:750; viewHeight=r?750:1334;
            fitWindow(); RECT sized{}; GetWindowRect(hwnd,&sized); sizingStart=sized;
            SIZE border=windowBorder(hwnd);
            for(UINT edge=WMSZ_LEFT;edge<=WMSZ_BOTTOMRIGHT;++edge) {
                RECT proposed=sized; proposed.right+=90; proposed.bottom+=45;
                SendMessageW(hwnd,WM_SIZING,edge,reinterpret_cast<LPARAM>(&proposed));
                double width=proposed.right-proposed.left-border.cx;
                double height=proposed.bottom-proposed.top-border.cy-ToolbarHeight;
                controls=controls&&std::abs(width*viewHeight-height*viewWidth)<=viewHeight;
            }
        }
        controls=controls&&SendMessageW(hwnd,WM_ERASEBKGND,0,0)==1;
        LocalFree(argv); DestroyWindow(hwnd); WSACleanup(); return controls?0:1;
    }
    bool autoConnect=argv&&argc>2&&std::wstring(argv[2])==L"--connect";
    if(argv&&argc>1) SetWindowTextW(configEdit,argv[1]); if(argv) LocalFree(argv);
    fitWindow(); ShowWindow(hwnd,show);
    if(autoConnect) connectClient();
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        bool shortcut=message.message==WM_KEYDOWN&&(message.wParam==VK_ESCAPE||
                      (message.wParam>=VK_F1&&message.wParam<=VK_F6)||
                      ((GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='T')||
                      (message.wParam==VK_RETURN&&GetFocus()==bitrateEdit));
        if(shortcut) SendMessageW(hwnd,message.message,message.wParam,message.lParam);
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    connection.reset(); WSACleanup(); return 0;
}

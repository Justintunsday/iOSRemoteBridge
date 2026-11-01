#include "Connection.h"
#include "irb_protocol.h"
#include "RpcReply.h"
#include "BufferedRenderer.h"
#include "D3DRenderer.h"
#include "HIDKeyMap.h"
#include "ExperimentalFSR.h"
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <cwchar>
using json=nlohmann::json;
namespace {
constexpr UINT FrameMessage=WM_APP+1,StatusMessage=WM_APP+2,ResponseMessage=WM_APP+3,KeyboardModeMessage=WM_APP+4;
constexpr UINT_PTR KeyboardTimer=1;
constexpr int ExpandedToolbarHeight=280;
constexpr UINT ToggleToolbarSystem=0x0100;
HWND window,configEdit,textEdit,bitrateEdit,controlCheck,notice;
HWND browseButton,connectButton,disconnectButton,homeButton,switcherButton,volumeUpButton,volumeDownButton;
HWND sendTextButton,bitrateLabel,rotateButton,fitButton,ratioCheck,sharpnessSlider,sharpnessLabel;
HWND toggleToolbarButton;
HWND videoWindow,vsyncCheck,fsrCheck;
HWND keyboardCheck,shortcutHint;
HWND experimentalCheck,experimentalVersions;
BufferedRenderer renderer;
D3DRenderer gpuRenderer;
ExperimentalFSR experimentalRenderer;
VideoFrame experimentalFrame;
bool experimentalEnabled=false;
bool advancedFailed=false;
std::unique_ptr<Connection> connection;
std::mutex frameMutex,statusMutex;
struct DisplayFrame { VideoFrame pixels; int rotation=0; };
std::shared_ptr<const DisplayFrame> frame;
std::wstring status;
std::string responseNotice;
std::wstring lastRenderNotice;
std::set<uint16_t> heldKeys;
bool keyboardApproved=false,keyboardPending=false;
uint64_t connectionEpoch=0;
ULONGLONG lastGesture=0;
ULONGLONG drawSince=0;
unsigned drawCount=0;
double drawFPS=0;
ULONGLONG noticeUntil=0;
std::atomic<int> rotation{0};
std::atomic<bool> framePosted{false};
int presentedRotation=0,currentFPS=60,sharpness=40;
double currentScale=1;
RECT videoRect{};
bool dragging=false;
bool keepRatio=true,fitNextFrame=true,toolbarVisible=true;
int viewWidth=750,viewHeight=1334;
RECT sizingStart{};
double lastX=0,lastY=0;
ULONGLONG lastMove=0;
enum {Config=10,Browse,Connect,Disconnect,Home,Switcher,VolumeUp,VolumeDown,SendText,Bitrate,Rotate,Fit,KeepRatio,Sharpness,ToggleToolbar,VSync,FSR,KeyboardMode,ExperimentalMode,ExperimentalVersion};
int toolbarHeight() { return toolbarVisible?ExpandedToolbarHeight:0; }
int minimumClientWidth() {
    int minimum=toolbarVisible?380:160;
    return keepRatio?std::max(minimum,static_cast<int>(std::ceil(120.0*viewWidth/viewHeight))):minimum;
}
std::array<HWND,26> toolbarControls() {
    return {configEdit,browseButton,connectButton,disconnectButton,controlCheck,homeButton,
            switcherButton,volumeUpButton,volumeDownButton,textEdit,sendTextButton,bitrateEdit,
            bitrateLabel,rotateButton,fitButton,ratioCheck,sharpnessLabel,sharpnessSlider,notice,toggleToolbarButton,vsyncCheck,fsrCheck,keyboardCheck,shortcutHint,experimentalCheck,experimentalVersions};
}
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
    RECT client{}; GetClientRect(window,&client);
    if(videoWindow) MoveWindow(videoWindow,0,toolbarHeight(),width,std::max(1,static_cast<int>(client.bottom)-toolbarHeight()),TRUE);
    if(!toolbarVisible) return;
    auto place=[](HWND child,int x,int y,int w,int h=24) { MoveWindow(child,x,y,std::max(1,w),h,TRUE); };
    place(configEdit,8,6,width-106); place(browseButton,width-92,6,84);
    place(connectButton,8,36,66); place(disconnectButton,80,36,76);
    place(controlCheck,162,36,128); place(fitButton,296,36,54);
    place(homeButton,8,66,54); place(switcherButton,68,66,90);
    place(volumeUpButton,164,66,44); place(volumeDownButton,214,66,44); place(rotateButton,264,66,64);
    int textWidth=width-204;
    place(textEdit,8,96,textWidth); place(sendTextButton,14+textWidth,96,70);
    place(bitrateEdit,90+textWidth,96,60); place(bitrateLabel,156+textWidth,100,40,20);
    place(ratioCheck,8,126,126); place(sharpnessLabel,140,130,78,20);
    place(sharpnessSlider,218,123,width-226,28); place(notice,8,245,width-16,30);
    place(vsyncCheck,8,155,90); place(fsrCheck,104,155,100);
    place(keyboardCheck,8,185,200); place(shortcutHint,214,189,width-222,20);
    place(experimentalCheck,8,215,210); place(experimentalVersions,224,215,width-232,110);
    place(toggleToolbarButton,width-134,155,126,26);
}
SIZE windowBorder(HWND hwnd) {
    RECT rect{};
    AdjustWindowRectEx(&rect,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,
                      static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)));
    return {rect.right-rect.left,rect.bottom-rect.top};
}
RECT videoArea() { RECT area{}; GetClientRect(window,&area); area.top=std::min<long>(toolbarHeight(),area.bottom); return area; }
void setSharpness(int strength) {
    sharpness=std::clamp(strength,0,100); renderer.setSharpening(sharpness); gpuRenderer.setSharpening(sharpness);
    SendMessageW(sharpnessSlider,TBM_SETPOS,TRUE,sharpness);
    SetWindowTextW(sharpnessLabel,(L"Sharp: "+std::to_wstring(sharpness)+L"%").c_str());
    InvalidateRect(videoWindow,nullptr,FALSE);
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
bool command(json request) {
    if(!connection||!connection->command(std::move(request))) {
        SetWindowTextW(notice,L"Connect and approve on the iPhone before sending commands."); return false;
    }
    return true;
}
bool releaseHeldKeys() {
    auto keys=std::move(heldKeys); heldKeys.clear(); bool ok=true;
    if(connection&&connection->authorized()) for(uint16_t usage:keys)
        if(!connection->command({{"op","key_event"},{"usage",usage},{"down",false}})) ok=false;
    return ok;
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
    int availableHeight=std::max(1L,monitor.rcWork.bottom-monitor.rcWork.top-border.cy-toolbarHeight()-32);
    double scale=std::min({1.0,static_cast<double>(availableWidth)/viewWidth,static_cast<double>(availableHeight)/viewHeight});
    int clientWidth=std::min(availableWidth,std::max(minimumClientWidth(),static_cast<int>(std::lround(viewWidth*scale))));
    int clientHeight=std::min(availableHeight,static_cast<int>(std::lround(viewHeight*scale)))+toolbarHeight();
    int width=clientWidth+border.cx,height=clientHeight+border.cy;
    RECT current{}; GetWindowRect(window,&current);
    int left=std::clamp((current.left+current.right-width)/2,monitor.rcWork.left,monitor.rcWork.right-width);
    int top=std::clamp((current.top+current.bottom-height)/2,monitor.rcWork.top,monitor.rcWork.bottom-height);
    SetWindowPos(window,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
void setToolbarVisible(bool visible) {
    if(toolbarVisible==visible) return;
    endDrag();
    if(GetCapture()) ReleaseCapture();
    toolbarVisible=visible; SetFocus(window);
    for(HWND child:toolbarControls()) ShowWindow(child,visible?SW_SHOWNA:SW_HIDE);
    CheckMenuItem(GetSystemMenu(window,FALSE),ToggleToolbarSystem,MF_BYCOMMAND|(visible?MF_CHECKED:MF_UNCHECKED));
    if(!IsZoomed(window)&&!IsIconic(window)) {
        if(keepRatio) fitWindow();
        else {
            RECT client{}; GetClientRect(window,&client);
            SIZE border=windowBorder(window);
            int width=std::max(static_cast<int>(client.right),minimumClientWidth())+border.cx;
            int height=std::max(static_cast<int>(client.bottom),toolbarHeight()+120)+border.cy;
            SetWindowPos(window,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    RECT client{}; GetClientRect(window,&client); layoutControls(client.right);
    videoRect={}; InvalidateRect(window,nullptr,FALSE);
}
void constrainSize(HWND hwnd,UINT edge,RECT &rect) {
    SIZE border=windowBorder(hwnd);
    int width=std::max(minimumClientWidth(),static_cast<int>(rect.right-rect.left-border.cx));
    int height=std::max(1,static_cast<int>(rect.bottom-rect.top-border.cy-toolbarHeight()));
    bool fromWidth=edge==WMSZ_LEFT||edge==WMSZ_RIGHT;
    if(edge!=WMSZ_TOP&&edge!=WMSZ_BOTTOM&&!fromWidth) {
        double dx=std::abs((rect.right-rect.left)-(sizingStart.right-sizingStart.left));
        double dy=std::abs((rect.bottom-rect.top)-(sizingStart.bottom-sizingStart.top));
        fromWidth=dx>=dy*viewWidth/viewHeight;
    }
    if(fromWidth) height=std::max(1,static_cast<int>(std::lround(static_cast<double>(width)*viewHeight/viewWidth)));
    else {
        width=std::max(minimumClientWidth(),static_cast<int>(std::lround(static_cast<double>(height)*viewWidth/viewHeight)));
        height=std::max(1,static_cast<int>(std::lround(static_cast<double>(width)*viewHeight/viewWidth)));
    }
    width+=border.cx; height+=border.cy+toolbarHeight();
    if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT) rect.left=rect.right-width;
    else rect.right=rect.left+width;
    if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT) rect.top=rect.bottom-height;
    else rect.bottom=rect.top+height;
}
void disconnect() {
    releaseHeldKeys(); endDrag(); ++connectionEpoch;
    keyboardApproved=keyboardPending=false; KillTimer(window,KeyboardTimer);
    SendMessageW(keyboardCheck,BM_SETCHECK,BST_UNCHECKED,0); EnableWindow(keyboardCheck,TRUE);
    if(connection) connection->close(); connection.reset();
    { std::lock_guard<std::mutex> lock(frameMutex); frame.reset(); }
    InvalidateRect(videoWindow,nullptr,FALSE); SetWindowTextW(window,L"iOSRemoteBridge - disconnected");
}
void connectClient() {
    disconnect();
    uint64_t epoch=connectionEpoch;
    fitNextFrame=true;
    try {
        std::ifstream stream{std::filesystem::path(editText(configEdit))};
        if(!stream) throw std::runtime_error("Select the local client-config.json pairing file");
        json config; stream>>config;
        bool wantsControl=SendMessageW(controlCheck,BM_GETCHECK,0,0)==BST_CHECKED;
        connection=std::make_unique<Connection>(config,wantsControl,currentFPS);
        connection->onFrame=[](VideoFrame &&source) {
            int r=rotation.load();
            auto shown=std::make_shared<DisplayFrame>();
            shown->pixels=rotate(std::move(source),r); shown->rotation=r;
            { std::lock_guard<std::mutex> lock(frameMutex); frame=std::move(shown); }
            if(!framePosted.exchange(true)) PostMessageW(window,FrameMessage,0,0);
        };
        connection->onStatus=[](const std::string &message) {
            { std::lock_guard<std::mutex> lock(statusMutex); status=wide(message); }
            PostMessageW(window,StatusMessage,0,0);
        };
        connection->onAuthorization=[epoch](bool) { PostMessageW(window,KeyboardModeMessage,static_cast<WPARAM>(epoch),0); };
        connection->onResponse=[epoch](const json &reply) {
            if(reply.value("op",std::string())=="keyboard_mode") {
                bool enabled=responseSucceeded(reply)&&reply.contains("result")&&reply["result"].value("keyboard_enabled",false);
                PostMessageW(window,KeyboardModeMessage,static_cast<WPARAM>(epoch),enabled?1:0);
            }
            if(reply.contains("ok")&&!responseSucceeded(reply)) {
                { std::lock_guard<std::mutex> lock(statusMutex); responseNotice=reply.value("error",std::string("operation failed")); }
                PostMessageW(window,ResponseMessage,0,0);
            }
        };
        SetWindowTextW(notice,L"Approve on the iPhone. Esc disconnects.");
        connection->start();
    } catch(const std::exception &) { SetWindowTextW(notice,L"Cannot load pairing file or configuration. Check the local JSON file and required fields."); connection.reset(); }
}
void toggleKeyboardMode() {
    if(keyboardPending) { SetWindowTextW(notice,L"Approve or Reject on the iPhone; Esc disconnects."); return; }
    if(!connection||!connection->authorized()) {
        SendMessageW(keyboardCheck,BM_SETCHECK,BST_UNCHECKED,0);
        SetWindowTextW(notice,L"Connect with Request control and approve on the iPhone first."); return;
    }
    endDrag(); releaseHeldKeys();
    bool enable=!keyboardApproved;
    if(!command({{"op","keyboard_mode"},{"enabled",enable}})) return;
    keyboardApproved=false; keyboardPending=true;
    SendMessageW(keyboardCheck,BM_SETCHECK,enable?BST_CHECKED:BST_UNCHECKED,0); EnableWindow(keyboardCheck,FALSE);
    SetTimer(window,KeyboardTimer,50000,nullptr);
    SetWindowTextW(notice,enable?L"Allow live keyboard input on the iPhone.":L"Disabling live keyboard input...");
}
bool videoFocused() { HWND focus=GetFocus(); return focus==window||focus==videoWindow; }
bool shortcutFocused() {
    HWND focus=GetFocus();
    return (focus==window||IsChild(window,focus))&&focus!=configEdit&&focus!=textEdit&&
           focus!=bitrateEdit&&focus!=sharpnessSlider&&focus!=experimentalVersions;
}
void scrollGesture(bool up) {
    if(keyboardPending||GetTickCount64()-lastGesture<300) return;
    endDrag(); lastGesture=GetTickCount64();
    double x1,y1,x2,y2;
    irb_unrotate(.5,up?.75:.25,presentedRotation,&x1,&y1);
    irb_unrotate(.5,up?.25:.75,presentedRotation,&x2,&y2);
    command({{"op","swipe"},{"x",x1},{"y",y1},{"to_x",x2},{"to_y",y2},{"duration_ms",230}});
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
    return irb_unrotate((px-videoRect.left)/(videoRect.right-videoRect.left),
                        (py-videoRect.top)/(videoRect.bottom-videoRect.top),presentedRotation,&x,&y)!=0;
}
LRESULT CALLBACK videoProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE) {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; MapWindowPoints(hwnd,window,&p,1);
        return SendMessageW(window,message,w,MAKELPARAM(p.x,p.y));
    }
    if(message==WM_CONTEXTMENU) return SendMessageW(window,message,reinterpret_cast<WPARAM>(window),l);
    if(message==WM_MOUSEWHEEL) return SendMessageW(window,message,w,l);
    if(message==WM_PAINT) {
        PAINTSTRUCT paint; HDC dc=BeginPaint(hwnd,&paint); RECT area{}; GetClientRect(hwnd,&area);
        std::shared_ptr<const DisplayFrame> shown;
        { std::lock_guard<std::mutex> lock(frameMutex); shown=frame; }
        const VideoFrame empty; const VideoFrame &pixels=shown?shown->pixels:empty;
        const VideoFrame *displayPixels=&pixels;
        bool experimentalActive=false,experimentalAttempted=false;
        if(experimentalEnabled&&pixels.width>0&&pixels.height>0) {
            double scale=std::min(static_cast<double>(area.right)/pixels.width,static_cast<double>(area.bottom)/pixels.height);
            int width=std::max(1,static_cast<int>(std::lround(pixels.width*scale)));
            int height=std::max(1,static_cast<int>(std::lround(pixels.height*scale)));
            if(width>pixels.width&&height>pixels.height) {
                experimentalAttempted=true;
                if(experimentalRenderer.process(pixels,width,height,experimentalFrame)) {
                    displayPixels=&experimentalFrame; experimentalActive=true;
                }
            }
        }
        gpuRenderer.setFSR(!experimentalActive&&SendMessageW(fsrCheck,BM_GETCHECK,0,0)==BST_CHECKED);
        RECT image{};
        bool painted=gpuRenderer.paint(hwnd,area.right,area.bottom,*displayPixels,image);
        if(painted&&connection&&connection->authorized()&&!keyboardPending) {
            ULONGLONG now=GetTickCount64(); if(!drawSince) drawSince=now;
            ++drawCount;
            if(now-drawSince>=1000) { drawFPS=1000.0*drawCount/(now-drawSince); drawCount=0; drawSince=now; }
            bool sync=SendMessageW(vsyncCheck,BM_GETCHECK,0,0)==BST_CHECKED;
            bool fsr=SendMessageW(fsrCheck,BM_GETCHECK,0,0)==BST_CHECKED;
            std::wstring scaling=experimentalActive?L" | FSR "+wide(experimentalRenderer.provider())+L" single-frame":
                experimentalEnabled?(experimentalAttempted?L" | Experimental FSR unavailable":L" | Experimental FSR ready"):
                gpuRenderer.fsrActive()?L" | FSR1 active":fsr?L" | FSR1 auto":L" | FSR1 off";
            std::wstring display=wide(gpuRenderer.backend())+(sync?L" | VSync on":L" | VSync off")+scaling;
            wchar_t drawText[40]{}; std::swprintf(drawText,40,L" | draw %.1f FPS",drawFPS); display+=drawText;
            if(GetTickCount64()>=noticeUntil&&display!=lastRenderNotice) { lastRenderNotice=display; SetWindowTextW(notice,display.c_str()); }
        }
        if(!painted) {
            if(!advancedFailed) {
                advancedFailed=true; EnableWindow(vsyncCheck,FALSE); EnableWindow(fsrCheck,FALSE);
                experimentalEnabled=false; experimentalRenderer.reset(); EnableWindow(experimentalCheck,FALSE);
                SendMessageW(experimentalCheck,BM_SETCHECK,BST_UNCHECKED,0);
                SendMessageW(vsyncCheck,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(fsrCheck,BM_SETCHECK,BST_UNCHECKED,0);
                SetWindowTextW(notice,L"Advanced display unavailable; using basic display.");
                OutputDebugStringA(gpuRenderer.error().c_str());
            }
            painted=renderer.paint(dc,area.right,area.bottom,0,pixels,image);
        }
        if(painted) {
            videoRect=image; MapWindowPoints(hwnd,window,reinterpret_cast<POINT *>(&videoRect),2);
            presentedRotation=shown?shown->rotation:0;
        } else videoRect={};
        EndPaint(hwnd,&paint); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        window=hwnd;
        configEdit=control(L"EDIT",L"client-config.json",WS_BORDER|ES_AUTOHSCROLL,8,8,410,25,Config);
        browseButton=control(L"BUTTON",L"Browse...",0,424,8,98,25,Browse);
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
        sharpnessLabel=control(L"STATIC",L"Sharp: 40%",0,0,0,78,20,0);
        sharpnessSlider=control(TRACKBAR_CLASSW,L"Sharpening",TBS_HORZ|TBS_NOTICKS|TBS_TOOLTIPS|WS_TABSTOP,0,0,100,28,Sharpness);
        SendMessageW(sharpnessSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
        SendMessageW(sharpnessSlider,TBM_SETLINESIZE,0,5);
        SendMessageW(sharpnessSlider,TBM_SETPAGESIZE,0,10);
        SendMessageW(sharpnessSlider,TBM_SETPOS,TRUE,sharpness);
        notice=control(L"STATIC",L"Choose a pairing file, then connect.",SS_NOPREFIX,8,72,1000,22,0);
        toggleToolbarButton=control(L"BUTTON",L"Hide tools (F9)",0,0,0,126,26,ToggleToolbar);
        vsyncCheck=control(L"BUTTON",L"VSync",BS_AUTOCHECKBOX,0,0,90,24,VSync);
        fsrCheck=control(L"BUTTON",L"FSR 1",BS_AUTOCHECKBOX,0,0,100,24,FSR);
        keyboardCheck=control(L"BUTTON",L"Keyboard+mouse (F10)",BS_AUTOCHECKBOX,0,0,200,24,KeyboardMode);
        shortcutHint=control(L"STATIC",L"H Home / C Control",SS_NOPREFIX,0,0,160,20,0);
        experimentalCheck=control(L"BUTTON",L"FSR 4/3/2.3 (exp.)",BS_AUTOCHECKBOX,0,0,210,24,ExperimentalMode);
        experimentalVersions=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,0,0,140,110,ExperimentalVersion);
        for(const wchar_t *name:{L"Auto 4 > 3 > 2.3",L"FSR 3 > 2.3",L"FSR 2.3"}) SendMessageW(experimentalVersions,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(experimentalVersions,CB_SETCURSEL,0,0);
        SendMessageW(vsyncCheck,BM_SETCHECK,BST_CHECKED,0);
        videoWindow=CreateWindowW(L"iOSRemoteBridge.Video",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,1,1,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        AppendMenuW(GetSystemMenu(hwnd,FALSE),MF_SEPARATOR,0,nullptr);
        AppendMenuW(GetSystemMenu(hwnd,FALSE),MF_STRING|MF_CHECKED,ToggleToolbarSystem,L"Show toolbar\tF9");
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
        case ToggleToolbar: setToolbarVisible(!toolbarVisible); return 0;
        case KeyboardMode: toggleKeyboardMode(); return 0;
        case ExperimentalMode:
            experimentalEnabled=SendMessageW(experimentalCheck,BM_GETCHECK,0,0)==BST_CHECKED;
            experimentalRenderer.reset(); lastRenderNotice.clear(); InvalidateRect(videoWindow,nullptr,FALSE); return 0;
        case ExperimentalVersion:
            if(HIWORD(w)==CBN_SELCHANGE) {
                int selected=static_cast<int>(SendMessageW(experimentalVersions,CB_GETCURSEL,0,0));
                experimentalRenderer.setMaximumVersion(4-std::clamp(selected,0,2));
                lastRenderNotice.clear(); InvalidateRect(videoWindow,nullptr,FALSE);
            } return 0;
        case VSync:
            gpuRenderer.setVSync(SendMessageW(vsyncCheck,BM_GETCHECK,0,0)==BST_CHECKED);
            InvalidateRect(videoWindow,nullptr,FALSE); return 0;
        case FSR:
            gpuRenderer.setFSR(SendMessageW(fsrCheck,BM_GETCHECK,0,0)==BST_CHECKED);
            InvalidateRect(videoWindow,nullptr,FALSE); return 0;
        case KeepRatio:
            keepRatio=SendMessageW(ratioCheck,BM_GETCHECK,0,0)==BST_CHECKED;
            if(keepRatio) fitWindow(); return 0;
        } break;
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(l)==sharpnessSlider) {
            setSharpness(static_cast<int>(SendMessageW(sharpnessSlider,TBM_GETPOS,0,0))); return 0;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if(message==WM_SYSKEYDOWN&&w==VK_F4) break;
        if(w==VK_ESCAPE) { disconnect(); return 0; }
        if(w==VK_F1) { endDrag(); rotation=(rotation.load()+90)%360; return 0; }
        if(w==VK_F2) { configure(30,currentScale); return 0; }
        if(w==VK_F3) { configure(60,currentScale); return 0; }
        if(w==VK_F4) { configure(currentFPS,.5); return 0; }
        if(w==VK_F5) { configure(currentFPS,1); return 0; }
        if(w==VK_F6) { fitWindow(); return 0; }
        if(w==VK_F7) { setSharpness(sharpness-10); return 0; }
        if(w==VK_F8) { setSharpness(sharpness+10); return 0; }
        if(w==VK_F9) { setToolbarVisible(!toolbarVisible); return 0; }
        if(w==VK_F10) { toggleKeyboardMode(); return 0; }
        if(keyboardApproved&&videoFocused()) {
            uint16_t usage=hidUsage(w,l);
            if(usage&&!heldKeys.count(usage)) {
                if(command({{"op","key_event"},{"usage",usage},{"down",true}})) heldKeys.insert(usage);
                else disconnect();
            }
            return 0;
        }
        if(keyboardPending&&shortcutFocused()) return 0;
        if(w==VK_RETURN&&GetFocus()==bitrateEdit) { command({{"op","configure"},{"bitrate",GetDlgItemInt(hwnd,Bitrate,nullptr,FALSE)*1000}}); return 0; }
        if((GetKeyState(VK_CONTROL)&0x8000)&&w=='T') { SetFocus(textEdit); return 0; }
        if(shortcutFocused()) {
            if((GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)) break;
            if(w=='H'&&!(l&(1LL<<30))) { command({{"op","press_home"}}); return 0; }
            if(w=='C'&&!(l&(1LL<<30))) { command({{"op","control_center"}}); return 0; }
            if(w==VK_ADD||w==VK_OEM_PLUS) { command({{"op","volume_up"}}); return 0; }
            if(w==VK_SUBTRACT||w==VK_OEM_MINUS) { command({{"op","volume_down"}}); return 0; }
            if(w==VK_UP||w==VK_DOWN) { scrollGesture(w==VK_UP); return 0; }
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        uint16_t usage=hidUsage(w,l);
        if(heldKeys.count(usage)) {
            if(command({{"op","key_event"},{"usage",usage},{"down",false}})) heldKeys.erase(usage);
            else disconnect();
            return 0;
        } break;
    }
    case WM_KILLFOCUS: if(!releaseHeldKeys()) disconnect(); return 0;
    case WM_ACTIVATEAPP: if(!w) { endDrag(); if(!releaseHeldKeys()) disconnect(); } break;
    case WM_TIMER:
        if(w==KeyboardTimer&&keyboardPending) {
            keyboardPending=keyboardApproved=false; SendMessageW(keyboardCheck,BM_SETCHECK,BST_UNCHECKED,0);
            EnableWindow(keyboardCheck,TRUE); KillTimer(hwnd,KeyboardTimer);
            SetWindowTextW(notice,L"Keyboard approval timed out; press F10 to try again."); return 0;
        } break;
    case WM_LBUTTONDOWN: {
        double x,y; SetFocus(hwnd);
        if(point(l,x,y)&&connection&&connection->authorized()&&!keyboardPending) {
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
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; ScreenToClient(hwnd,&p);
        if(PtInRect(&videoRect,p)) { scrollGesture(GET_WHEEL_DELTA_WPARAM(w)<0); return 0; }
        break;
    }
    case WM_CONTEXTMENU: {
        if(reinterpret_cast<HWND>(w)!=hwnd) break;
        POINT position{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(l==-1) { position={8,toolbarHeight()+8}; ClientToScreen(hwnd,&position); }
        else {
            POINT clientPoint=position; ScreenToClient(hwnd,&clientPoint);
            RECT client{}; GetClientRect(hwnd,&client);
            if(!PtInRect(&client,clientPoint)) break; // Keep the native title-bar menu.
            if(keyboardApproved) {
                endDrag();
                double x,y;
                if(point(MAKELPARAM(clientPoint.x,clientPoint.y),x,y))
                    command({{"op","long_press"},{"x",x},{"y",y},{"duration_ms",500}});
                return 0;
            }
        }
        endDrag();
        HMENU menu=CreatePopupMenu();
        if(menu) {
            AppendMenuW(menu,MF_STRING|(toolbarVisible?MF_CHECKED:MF_UNCHECKED),ToggleToolbar,L"Show toolbar\tF9");
            UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,position.x,position.y,0,hwnd,nullptr);
            DestroyMenu(menu);
            if(selected==ToggleToolbar) setToolbarVisible(!toolbarVisible);
        }
        return 0;
    }
    case WM_SYSCOMMAND:
        if((w&0xfff0)==ToggleToolbarSystem) { setToolbarVisible(!toolbarVisible); return 0; }
        break;
    case FrameMessage: {
        framePosted=false;
        int width,height;
        { std::lock_guard<std::mutex> lock(frameMutex); width=frame?frame->pixels.width:0; height=frame?frame->pixels.height:0; }
        if(width>0&&height>0) {
            bool changed=static_cast<int64_t>(width)*viewHeight!=static_cast<int64_t>(height)*viewWidth;
            viewWidth=width; viewHeight=height;
            bool fit=keepRatio&&(fitNextFrame||changed); fitNextFrame=false;
            if(fit&&!IsZoomed(hwnd)&&!IsIconic(hwnd)) fitWindow();
        }
        InvalidateRect(videoWindow,nullptr,FALSE); return 0;
    }
    case StatusMessage: { std::lock_guard<std::mutex> lock(statusMutex); SetWindowTextW(hwnd,(L"iOSRemoteBridge | "+status).c_str()); return 0; }
    case ResponseMessage: { std::lock_guard<std::mutex> lock(statusMutex); noticeUntil=GetTickCount64()+5000; lastRenderNotice.clear(); SetWindowTextW(notice,wide(responseNotice).c_str()); return 0; }
    case KeyboardModeMessage:
        if(static_cast<uint64_t>(w)==connectionEpoch) {
            keyboardApproved=l!=0; keyboardPending=false; heldKeys.clear(); KillTimer(hwnd,KeyboardTimer);
            SendMessageW(keyboardCheck,BM_SETCHECK,keyboardApproved?BST_CHECKED:BST_UNCHECKED,0); EnableWindow(keyboardCheck,TRUE);
            SetWindowTextW(shortcutHint,keyboardApproved?L"Keys sent to iPhone":L"H Home / C Control");
            if(keyboardApproved) SetFocus(hwnd);
            lastRenderNotice.clear();
        } return 0;
    case WM_ENTERSIZEMOVE: endDrag(); GetWindowRect(hwnd,&sizingStart); return 0;
    case WM_SIZING:
        if(keepRatio) { constrainSize(hwnd,static_cast<UINT>(w),*reinterpret_cast<RECT *>(l)); return TRUE; }
        break;
    case WM_GETMINMAXINFO: {
        SIZE border=windowBorder(hwnd); auto *limits=reinterpret_cast<MINMAXINFO *>(l);
        limits->ptMinTrackSize.x=minimumClientWidth()+border.cx;
        limits->ptMinTrackSize.y=toolbarHeight()+120+border.cy; return 0;
    }
    case WM_SIZE:
        if(w!=SIZE_MINIMIZED) { layoutControls(LOWORD(l)); videoRect={}; InvalidateRect(hwnd,nullptr,FALSE); }
        return 0;
    case WM_ERASEBKGND: return 1; // Buffered WM_PAINT owns the entire client background.
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(hwnd,&paint); RECT area; GetClientRect(hwnd,&area);
        FillRect(dc,&area,GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(hwnd,&paint); return 0;
    }
    case WM_CLOSE: disconnect(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY: experimentalRenderer.reset(); gpuRenderer.reset(); renderer.reset(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 1;
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX),ICC_BAR_CLASSES};
    if(!InitCommonControlsEx(&common)) { WSACleanup(); return 1; }
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=procedure; cls.lpszClassName=L"iOSRemoteBridge.Window";
    cls.hCursor=LoadCursor(nullptr,IDC_ARROW); cls.hbrBackground=nullptr;
    RegisterClassW(&cls);
    WNDCLASSW videoClass{}; videoClass.hInstance=instance; videoClass.lpfnWndProc=videoProcedure;
    videoClass.lpszClassName=L"iOSRemoteBridge.Video"; videoClass.hCursor=LoadCursor(nullptr,IDC_ARROW);
    videoClass.style=CS_HREDRAW|CS_VREDRAW; RegisterClassW(&videoClass);
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"iOSRemoteBridge - pair and connect",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                           CW_USEDEFAULT,CW_USEDEFAULT,460,950,nullptr,nullptr,instance,nullptr);
    if(!hwnd) { WSACleanup(); return 1; }
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv&&argc>1&&std::wstring(argv[1])==L"--self-test-ui") {
        bool controls=GetDlgItem(hwnd,Config)&&GetDlgItem(hwnd,Connect)&&GetDlgItem(hwnd,Disconnect)&&
                      GetDlgItem(hwnd,SendText)&&GetDlgItem(hwnd,Fit)&&GetDlgItem(hwnd,Rotate)&&
                      GetDlgItem(hwnd,Sharpness)&&SendMessageW(sharpnessSlider,TBM_GETPOS,0,0)==40&&
                      videoWindow&&SendMessageW(vsyncCheck,BM_GETCHECK,0,0)==BST_CHECKED&&
                      SendMessageW(experimentalCheck,BM_GETCHECK,0,0)==BST_UNCHECKED&&
                      hidUsage('H',0)==0x0b&&hidUsage(VK_OEM_PLUS,0)==0x2e&&
                      hidUsage(VK_SHIFT,0x36LL<<16)==0xe5&&hidUsage(VK_CONTROL,1LL<<24)==0xe4&&
                      SendMessageW(ratioCheck,BM_GETCHECK,0,0)==BST_CHECKED&&
                      GetDlgItemInt(hwnd,Bitrate,nullptr,FALSE)==8000;
        for(int mode=0;mode<2;++mode) {
            if(mode) SendMessageW(hwnd,WM_COMMAND,ToggleToolbar,0);
            controls=controls&&videoArea().top==(mode?0:ExpandedToolbarHeight);
            for(HWND child:toolbarControls()) {
                bool visible=(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;
                controls=controls&&(visible==(mode==0));
            }
            for(int r=0;r<2;++r) {
                viewWidth=r?1334:750; viewHeight=r?750:1334;
                fitWindow(); RECT sized{}; GetWindowRect(hwnd,&sized); sizingStart=sized;
                SIZE border=windowBorder(hwnd);
                for(UINT edge=WMSZ_LEFT;edge<=WMSZ_BOTTOMRIGHT;++edge) {
                    RECT proposed=sized; proposed.right+=90; proposed.bottom+=45;
                    SendMessageW(hwnd,WM_SIZING,edge,reinterpret_cast<LPARAM>(&proposed));
                    double width=proposed.right-proposed.left-border.cx;
                    double height=proposed.bottom-proposed.top-border.cy-toolbarHeight();
                    controls=controls&&std::abs(width*viewHeight-height*viewWidth)<=viewHeight;
                }
                MINMAXINFO limits{}; SendMessageW(hwnd,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
                RECT smallest{sized.left,sized.top,sized.left+1,sized.top+1};
                SendMessageW(hwnd,WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&smallest));
                controls=controls&&smallest.right-smallest.left>=limits.ptMinTrackSize.x&&
                         smallest.bottom-smallest.top>=limits.ptMinTrackSize.y;
            }
        }
        SendMessageW(hwnd,WM_KEYDOWN,VK_F9,0);
        controls=controls&&toolbarVisible&&SendMessageW(sharpnessSlider,TBM_GETPOS,0,0)==40&&
                 SendMessageW(ratioCheck,BM_GETCHECK,0,0)==BST_CHECKED;
        SendMessageW(hwnd,WM_SYSCOMMAND,ToggleToolbarSystem,0);
        controls=controls&&!toolbarVisible;
        controls=controls&&SendMessageW(hwnd,WM_ERASEBKGND,0,0)==1;
        LocalFree(argv); DestroyWindow(hwnd); WSACleanup(); return controls?0:1;
    }
    bool autoConnect=argv&&argc>2&&std::wstring(argv[2])==L"--connect";
    if(argv&&argc>1) SetWindowTextW(configEdit,argv[1]); if(argv) LocalFree(argv);
    fitWindow(); ShowWindow(hwnd,show);
    if(autoConnect) connectClient();
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        bool navigation=message.message==WM_KEYDOWN&&!keyboardApproved&&shortcutFocused()&&
                        (message.wParam=='H'||message.wParam=='C'||message.wParam==VK_ADD||message.wParam==VK_OEM_PLUS||
                         message.wParam==VK_SUBTRACT||message.wParam==VK_OEM_MINUS||message.wParam==VK_UP||message.wParam==VK_DOWN);
        bool shortcut=message.message==WM_KEYDOWN&&(message.wParam==VK_ESCAPE||
                      (message.wParam>=VK_F1&&message.wParam<=VK_F10)||
                      (!keyboardApproved&&(GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='T')||
                      (message.wParam==VK_RETURN&&GetFocus()==bitrateEdit));
        if(shortcut||navigation) SendMessageW(hwnd,message.message,message.wParam,message.lParam);
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    connection.reset(); WSACleanup(); return 0;
}

#include "D3DRenderer.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok,const std::string &message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char **argv) {
    HWND window=nullptr;
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc=DefWindowProcW; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"IRB.GPU.Check";
        require(RegisterClassW(&cls)!=0,"Register diagnostic window");
        window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);
        require(window!=nullptr,"Create hidden diagnostic window");
        D3DRenderer renderer(argc>1&&std::string(argv[1])=="--warp"); renderer.setVSync(false);
        VideoFrame frame; frame.width=frame.height=32; frame.bgra.resize(32*32*4);
        VideoFrame output; RECT image{};
        auto paint=[&](int width,int height) {
            require(renderer.paint(window,width,height,frame,image,&output),renderer.error());
        };
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            size_t p=(y*32+x)*4; int level=x==31?255:x*8;
            frame.bgra[p]=frame.bgra[p+1]=frame.bgra[p+2]=static_cast<uint8_t>(level); frame.bgra[p+3]=255;
        }
        renderer.setSharpening(0); paint(32,32);
        for(size_t p=0;p<frame.bgra.size();p+=4) for(int c=0;c<3;++c)
            require(std::abs(static_cast<int>(output.bgra[p+c])-frame.bgra[p+c])<=1,"Native gray range changed");
        renderer.setFSR(true); paint(32,32); require(!renderer.fsrActive(),"FSR applied to native-size image");
        renderer.setSharpening(40);
        for(size_t p=0;p<frame.bgra.size();p+=4) { frame.bgra[p]=37; frame.bgra[p+1]=111; frame.bgra[p+2]=203; }
        paint(64,96); require(renderer.fsrActive(),"FSR did not activate for 2x scaling");
        require(image.top==16&&image.bottom==80&&image.left==0&&image.right==64,"GPU letterbox geometry changed");
        size_t center=(48*64+32)*4;
        require(std::abs(output.bgra[center]-37)<=1&&std::abs(output.bgra[center+1]-111)<=1&&std::abs(output.bgra[center+2]-203)<=1,
                "FSR/RCAS changed a flat color");
        require(output.bgra[4]==0&&output.bgra[5]==0&&output.bgra[6]==0,"Letterbox is not black");
        renderer.setSharpening(0);
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            size_t p=(y*32+x)*4; uint8_t level=x>y?235:20;
            frame.bgra[p]=frame.bgra[p+1]=frame.bgra[p+2]=level;
        }
        renderer.setFSR(false); paint(64,64); auto linear=output.bgra;
        renderer.setFSR(true); paint(64,64);
        size_t difference=0;
        for(size_t p=0;p<linear.size();p+=4) difference+=std::abs(static_cast<int>(linear[p])-output.bgra[p]);
        require(difference>100,"FSR EASU output was indistinguishable from bilinear scaling");
        renderer.setVSync(true); paint(64,64);
        renderer.setVSync(false); paint(16,16); require(!renderer.fsrActive(),"FSR applied to downscaling");
        std::cout<<renderer.backend()<<": range, flat color, EASU pixels, RCAS, resize, letterboxing and Present 0/1 passed\n";
        renderer.reset(); DestroyWindow(window); return 0;
    } catch(const std::exception &error) { if(window) DestroyWindow(window); std::cerr<<error.what()<<'\n'; return 1; }
}

#include "irb_protocol.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
static void check(bool ok) { if(!ok) { std::cerr<<"protocol check failed\n"; std::exit(1); } }
int main() {
    std::array<uint8_t,IRB_HEADER_SIZE> b{};
    irb_header a{IRB_VIDEO,1,8000000,0xfefdfcfb,0x0102030405060708ULL}, out{};
    irb_pack(b.data(),a);
    check(b[8]==0 && b[9]==0x7a && b[10]==0x12 && b[11]==0);
    check(irb_unpack(b.data(),b.size(),&out));
    check(out.pts_us==a.pts_us&&out.sequence==a.sequence&&out.length==a.length);
    for(size_t n=0;n<b.size();++n) check(!irb_unpack(b.data(),n,&out));
    auto bad=b; bad[4]=2; check(!irb_unpack(bad.data(),bad.size(),&out));
    bad=b; bad[7]=1; check(!irb_unpack(bad.data(),bad.size(),&out));
    bad=b; bad[5]=99; check(!irb_unpack(bad.data(),bad.size(),&out));
    bad=b; bad[6]=128; check(!irb_unpack(bad.data(),bad.size(),&out));
    a.length=IRB_MAX_PAYLOAD+1; irb_pack(b.data(),a); check(!irb_unpack(b.data(),b.size(),&out));
    a.type=IRB_CONTROL; a.flags=0; a.length=IRB_MAX_CONTROL+1; irb_pack(b.data(),a);
    check(!irb_unpack(b.data(),b.size(),&out));
    a.length=IRB_MAX_CONTROL; irb_pack(b.data(),a); check(irb_unpack(b.data(),b.size(),&out));
    a.type=IRB_HID; irb_pack(b.data(),a); check(irb_unpack(b.data(),b.size(),&out));
    a.length=IRB_MAX_CONTROL+1; irb_pack(b.data(),a); check(!irb_unpack(b.data(),b.size(),&out));
    double x=0,y=0;
    check(irb_unrotate(.2,.3,90,&x,&y)&&fabs(x-.3)<1e-9&&fabs(y-.8)<1e-9);
    check(irb_unrotate(.2,.3,270,&x,&y)&&fabs(x-.7)<1e-9&&fabs(y-.2)<1e-9);
    check(irb_unrotate(0,1,180,&x,&y)&&x==1&&y==0);
    check(!irb_unrotate(-.1,0,0,&x,&y));
    check(!irb_unrotate(std::numeric_limits<double>::quiet_NaN(),0,0,&x,&y));
    check(!irb_unrotate(0,0,45,&x,&y));
    std::cout<<"wire limits, byte order, truncation, flags, coordinates: passed\n";
}

#include "../client/windows/MouseMotion.h"
#include <cstdlib>
#include <iostream>
#include <limits>
static void check(bool ok) { if(!ok) { std::cerr<<"mouse motion check failed\n";std::exit(1); } }
int main() {
    MouseMotion motion;
    motion.add(50000,-60000,0); auto first=motion.take(),second=motion.take();
    check(first.first==32767&&first.second==-32767);
    check(first.first+second.first==50000&&first.second+second.second==-60000&&!motion.pending());
    const int expected[][2]={{12,7},{7,-12},{-12,-7},{-7,12}};
    for(int i=0;i<4;++i) { motion.add(12,7,i*90);auto delta=motion.take();check(delta.first==expected[i][0]&&delta.second==expected[i][1]); }
    motion.add(100,20,0);motion.add(-100,-20,0);check(!motion.pending());
    motion.add(50,60,0);motion.clear();check(!motion.pending()&&motion.take()==std::make_pair(0,0));
    motion.add(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),180);
    auto bounded=motion.take();check(bounded.first==32767&&bounded.second==-32767);
    std::cout<<"relative motion coalescing, signed bounds, rotations and release discard passed\n";
}

// src/worldmap_tiles.cpp: the tiles file's head checks, WIC decoding and the background queue (offline).
// Run by tests/worldmap_tiles_test.py with a pyramid it builds from eight flat-coloured 128 px sources (tile 8 px,
// tools/worldmap_basemap.py): worldmap_tiles_test <tiles file> <a truncated copy> <tile> <levels> <format>.
#include "../src/worldmap_tiles.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace worldmap;

namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s\n",what);}
}
int Near(int a,int b) { return std::abs(a-b)<=10; }
// tests/test_worldmap_pack.py colour(): source column c (A..D), row r (1..2).
bool Colour(std::uint32_t bgra,int c,int r) {
    const int b=static_cast<int>(bgra&0xFF),g=static_cast<int>((bgra>>8)&0xFF),red=static_cast<int>((bgra>>16)&0xFF);
    return Near(red,40+60*c) && Near(g,200-120*r) && Near(b,90);
}
}  // namespace

int wmain(int argc,wchar_t** argv) {
    if(argc!=6){std::printf("usage: worldmap_tiles_test FILE TRUNCATED TILE LEVELS FORMAT\n");return 2;}
    const int tile=_wtoi(argv[3]),levels=_wtoi(argv[4]);
    const std::uint32_t format=static_cast<std::uint32_t>(_wtoi(argv[5]));
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);

    TileStore bad;
    Expect(!bad.Open(L"Z:\\no such file.bin",tile,levels,format) && bad.Why(),"a missing file refused");
    Expect(!bad.Open(argv[2],tile,levels,format),"a truncated file refused");
    Expect(!bad.Open(argv[1],tile+1,levels,format),"another tile size refused");
    Expect(!bad.Open(argv[1],tile,levels,format+1),"another format refused");

    TileStore store;
    Expect(store.Open(argv[1],tile,levels,format),"the file opens");
    Expect(store.Count()==2731,"every tile in the table");

    // Synchronous: level 0 is one tile x half a tile; the deepest level's tiles show their source.
    std::vector<std::uint8_t> bytes;
    TilePixels px;
    Expect(store.Read(0,bytes) && DecodeJpeg(bytes.data(),bytes.size(),px) && px.w==tile && px.h==tile/2,"level 0: the Earth");
    const int deepest=levels-1,cols=1<<deepest,rows=1<<(deepest-1);
    int base=0;
    for(int z=0;z<deepest;++z)base+=(1<<z)*(z>0 ? 1<<(z-1) : 1);
    bool all=true;
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
        TilePixels t;
        all=all && store.Read(base+y*cols+x,bytes) && DecodeJpeg(bytes.data(),bytes.size(),t) && t.w==tile && t.h==tile &&
            Colour(t.bgra[static_cast<std::size_t>(t.w*(t.h/2)+t.w/2)],x*4/cols,y*2/rows);
    }
    Expect(all,"the deepest level: every tile decodes and shows its source");
    const std::uint8_t junk[16]={1,2,3};
    Expect(!DecodeJpeg(junk,sizeof junk,px) && px.w==0,"not a JPEG: refused");
    Expect(!store.Read(-1,bytes) && !store.Read(store.Count(),bytes),"outside the table");

    // The background queue: the last Want wins, everything wanted comes back once, decoded.
    const int first[3]={1,2,3},wanted[4]={base,base+1,base+cols,-5};
    store.Want(first,3);
    store.Want(wanted,4);
    std::vector<int> got;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<until && got.size()<3) {
        TilePixels t;
        if(store.Take(t)) {
            if(t.w==tile)got.push_back(t.index);
        } else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool hasBase=false,hasCol=false;
    for(int i:got){hasBase=hasBase || i==base;hasCol=hasCol || i==base+cols;}
    Expect(hasBase && hasCol,"wanted tiles decoded on the worker");
    store.Close();
    Expect(!store.IsOpen(),"closed");

    std::printf("worldmap_tiles_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}

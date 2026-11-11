// The world map's base map tiles (docs/plan-2026-11-10-worldmap.md §2): the JPEG tile pyramid file
// (pylib/worldmap.py TILES_FILE: b'EDWM', u32 format, u32 tile, u32 levels, then per tile u64 offset + u32 size, then
// the JPEGs) read and decoded to BGRA on one background thread with WIC, so the render thread only uploads what is
// ready. No game memory: checked offline (tests/worldmap_tiles_test.cpp).
#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace worldmap {

struct TilePixels {
    int index=-1;                       // the tile's entry in the file's table (worldmap_logic.h TileIndex)
    int w=0,h=0;
    std::vector<std::uint32_t> bgra;    // w * h, rows top down
};

// One JPEG to BGRA (WIC; the calling thread must have COM initialised). False: not a JPEG WIC reads.
bool DecodeJpeg(const std::uint8_t* data,std::size_t size,TilePixels& out);

class TileStore {
public:
    TileStore()=default;
    TileStore(const TileStore&)=delete;
    TileStore& operator=(const TileStore&)=delete;
    ~TileStore() { Close(); }

    // Reads and checks the head (magic, format, tile, levels; every entry inside the file) and starts the decoder
    // thread. False (nothing open, why() says why): a missing or foreign file; the map is then not shown.
    bool Open(const wchar_t* path,int tile,int levels,std::uint32_t format);
    void Close();
    bool IsOpen() const noexcept { return file_!=nullptr; }
    const char* Why() const noexcept { return why_; }
    int Count() const noexcept { return static_cast<int>(entries_.size()); }

    // The tiles wanted now, most important first: replaces what is still queued (tiles already decoded and not yet
    // taken stay ready). Indices outside the table are ignored.
    void Want(const int* indices,int n);
    // One decoded tile, if any is ready (false: none yet). A tile that failed to decode comes back with w == 0.
    bool Take(TilePixels& out);
    // Synchronous read of one tile's JPEG bytes (tests; the decoder thread uses the same).
    bool Read(int index,std::vector<std::uint8_t>& out) const;

private:
    struct Entry { std::uint64_t offset; std::uint32_t size; };
    void Run();

    void* file_=nullptr;                 // HANDLE
    const char* why_="not opened";
    std::vector<Entry> entries_;
    std::thread worker_;
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<int> queue_;              // wanted, not started
    std::vector<int> busy_;              // started or ready, not taken
    std::deque<TilePixels> ready_;
    bool stop_=false;
};

}  // namespace worldmap

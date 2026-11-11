// See worldmap_tiles.h.
#include "worldmap_tiles.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

namespace worldmap {
namespace {

template<class T> struct Com {
    T* p=nullptr;
    ~Com() { if(p)p->Release(); }
    T** operator&() noexcept { return &p; }
    T* operator->() const noexcept { return p; }
};

bool ReadAt(HANDLE file,std::uint64_t offset,void* to,DWORD size) noexcept {
    OVERLAPPED at{};
    at.Offset=static_cast<DWORD>(offset);
    at.OffsetHigh=static_cast<DWORD>(offset>>32);
    DWORD got=0;
    return ReadFile(file,to,size,&got,&at) && got==size;
}

}  // namespace

bool DecodeJpeg(const std::uint8_t* data,std::size_t size,TilePixels& out) {
    out.w=out.h=0;
    out.bgra.clear();
    if(!data || !size || size>0x7FFFFFFF)return false;
    Com<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return false;
    Com<IWICStream> stream;
    if(FAILED(factory->CreateStream(&stream)) ||
       FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data),static_cast<DWORD>(size))))return false;
    Com<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromStream(stream.p,&GUID_ContainerFormatJpeg,WICDecodeMetadataCacheOnDemand,&decoder)))
        return false;
    Com<IWICBitmapFrameDecode> frame;
    if(FAILED(decoder->GetFrame(0,&frame)))return false;
    UINT w=0,h=0;
    if(FAILED(frame->GetSize(&w,&h)) || !w || !h || w>8192 || h>8192)return false;
    Com<IWICFormatConverter> convert;
    if(FAILED(factory->CreateFormatConverter(&convert)) ||
       FAILED(convert->Initialize(frame.p,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0.0,
                                  WICBitmapPaletteTypeCustom)))return false;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w)*h);
    if(FAILED(convert->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()*4),reinterpret_cast<BYTE*>(pixels.data()))))
        return false;
    out.w=static_cast<int>(w);
    out.h=static_cast<int>(h);
    out.bgra.swap(pixels);
    return true;
}

bool TileStore::Open(const wchar_t* path,int tile,int levels,std::uint32_t format) {
    Close();
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){why_="the file is missing";return false;}
    LARGE_INTEGER total{};
    std::uint8_t head[16]{};
    bool ok=GetFileSizeEx(file,&total) && ReadAt(file,0,head,sizeof head);
    std::uint32_t got[3]{};
    if(ok)std::memcpy(got,head+4,sizeof got);
    if(!ok || std::memcmp(head,"EDWM",4) || got[0]!=format || got[1]!=static_cast<std::uint32_t>(tile) ||
       got[2]!=static_cast<std::uint32_t>(levels) || levels<1 || levels>16) {
        CloseHandle(file);
        why_="not a tiles file of this layout";
        return false;
    }
    std::size_t count=0;
    for(int z=0;z<levels;++z)count+=(std::size_t{1}<<z)*(z>0 ? std::size_t{1}<<(z-1) : 1);
    std::vector<std::uint8_t> table(count*12);
    if(!ReadAt(file,16,table.data(),static_cast<DWORD>(table.size()))) {
        CloseHandle(file);
        why_="the table is cut short";
        return false;
    }
    std::vector<Entry> entries(count);
    const std::uint64_t end=static_cast<std::uint64_t>(total.QuadPart);
    for(std::size_t i=0;i<count;++i) {
        std::memcpy(&entries[i].offset,table.data()+i*12,8);
        std::memcpy(&entries[i].size,table.data()+i*12+8,4);
        if(!entries[i].size || entries[i].offset<16+table.size() || entries[i].offset+entries[i].size>end) {
            CloseHandle(file);
            why_="a tile lies outside the file";
            return false;
        }
    }
    file_=file;
    entries_.swap(entries);
    why_=nullptr;
    stop_=false;
    worker_=std::thread([this] { Run(); });
    return true;
}

void TileStore::Close() {
    if(worker_.joinable()) {
        {
            std::lock_guard<std::mutex> hold(lock_);
            stop_=true;
        }
        wake_.notify_all();
        worker_.join();
    }
    if(file_)CloseHandle(static_cast<HANDLE>(file_));
    file_=nullptr;
    entries_.clear();
    queue_.clear();
    busy_.clear();
    ready_.clear();
    if(!why_)why_="closed";
}

void TileStore::Want(const int* indices,int n) {
    {
        std::lock_guard<std::mutex> hold(lock_);
        queue_.clear();
        for(int i=0;i<n;++i) {
            const int index=indices[i];
            if(index<0 || index>=Count())continue;
            if(std::find(busy_.begin(),busy_.end(),index)!=busy_.end())continue;
            if(std::find(queue_.begin(),queue_.end(),index)!=queue_.end())continue;
            queue_.push_back(index);
        }
    }
    wake_.notify_one();
}

bool TileStore::Take(TilePixels& out) {
    std::lock_guard<std::mutex> hold(lock_);
    if(ready_.empty())return false;
    out=std::move(ready_.front());
    ready_.pop_front();
    busy_.erase(std::remove(busy_.begin(),busy_.end(),out.index),busy_.end());
    return true;
}

bool TileStore::Read(int index,std::vector<std::uint8_t>& out) const {
    if(!file_ || index<0 || index>=Count())return false;
    const Entry& e=entries_[static_cast<std::size_t>(index)];
    out.resize(e.size);
    return ReadAt(static_cast<HANDLE>(file_),e.offset,out.data(),e.size);
}

void TileStore::Run() {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::vector<std::uint8_t> bytes;
    for(;;) {
        int index=-1;
        {
            std::unique_lock<std::mutex> hold(lock_);
            wake_.wait(hold,[this] { return stop_ || !queue_.empty(); });
            if(stop_)break;
            index=queue_.front();
            queue_.pop_front();
            busy_.push_back(index);
        }
        TilePixels tile;
        if(!Read(index,bytes) || !DecodeJpeg(bytes.data(),bytes.size(),tile))tile=TilePixels{};
        tile.index=index;
        std::lock_guard<std::mutex> hold(lock_);
        ready_.push_back(std::move(tile));
    }
    if(SUCCEEDED(com))CoUninitialize();
}

}  // namespace worldmap

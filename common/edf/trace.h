#pragma once
// A recording of what a plugin step saw in the real game, frame by frame, for an offline test to replay through the same
// production code (docs/trace-replay.md). Written by the plugin (src/trace_rec.cpp, ini TraceFile), read by the tests
// (tests/trace_replay.h). The game's memory is recorded as it was at the step's start: each region (an object, an array,
// a vtable slot) by its address in the game's process, the replay putting it back at that very address so the step's
// pointers are its own; whatever is not memory the step can read (the listener, a class's name, the plugin's own tables)
// as a named value of an object. What the step did (its sounds, its tally) is recorded as outputs, to check a replay
// against when it runs the same sources the recording was made with.
//
// The file: kMagic, the header (Header), then records, each {u8 type, u32 size, size bytes}:
//   kReset   -                              the plugin's state reset (a mission begun): the replay resets its own too
//   kCall    u8 step, u64 object, u64 frame, u64 game ms   a step's call on `object` begins; its inputs follow
//   kRegion  u64 address, u32 bytes, u8 mode, data         mode 0: the bytes whole; mode 1: changes against the last of this
//                                                          address and size, runs of {u32 offset, u32 length, bytes} to
//                                                          a length 0
//   kValue   u64 object, name (u8 length + chars), bytes   what `name` of `object` was (0: of the game, not one object)
//   kOutput  name (u8 length + chars), bytes                what the step did, in order
// A region of the same address recorded again in one call replaces the first.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace edf::trace {
constexpr char kMagic[8]={'E','D','F','T','R','C','1','\0'};
constexpr std::uint32_t kVersion=1;
enum Type : std::uint8_t { kReset=1, kCall=2, kRegion=3, kValue=4, kOutput=5 };
enum Step : std::uint8_t { kVehicleSound=1 };
struct Header {
    std::uint32_t version;
    std::uint32_t pad;
    std::uint64_t image;       // EDF.dll's base in the recorded process (pointers into it compared, as crew::image)
    char sources[68];          // the SHA-256 (hex) of the step's sources the plugin was built from (CMake: EDF6_TRACE_SOURCES)
    char build[28];            // the plugin's build (its git id)
};
static_assert(sizeof(Header)==112);

// --- Writing (the plugin) ---
class Writer {
public:
    bool Open(const wchar_t* path,const Header& h) noexcept {
        Close();
        if(_wfopen_s(&f_,path,L"wb")!=0 || !f_){f_=nullptr;return false;}
        std::fwrite(kMagic,1,sizeof(kMagic),f_);
        std::fwrite(&h,1,sizeof(h),f_);
        return true;
    }
    void Close() noexcept { if(f_){std::fclose(f_);f_=nullptr;} last_.clear(); }
    bool IsOpen() const noexcept { return f_!=nullptr; }
    long long Bytes() const noexcept { return f_ ? std::ftell(f_) : 0; }

    void Reset() noexcept { Record(kReset,nullptr,0); }
    void Call(Step step,const void* object,std::uint64_t frame,std::uint64_t ms) noexcept {
        unsigned char b[25];
        b[0]=step;Put(b+1,reinterpret_cast<std::uint64_t>(object));Put(b+9,frame);Put(b+17,ms);
        Record(kCall,b,sizeof(b));
    }
    // `bytes` at `at` as the game has them now (the caller has made sure they can be read).
    void Region(const void* at,std::uint32_t bytes) noexcept {
        const auto address=reinterpret_cast<std::uint64_t>(at);
        const auto* now=static_cast<const unsigned char*>(at);
        std::vector<unsigned char>& was=last_[address];
        buf_.clear();
        Append(address);Append(bytes);
        if(was.size()!=bytes) {
            buf_.push_back(0);buf_.insert(buf_.end(),now,now+bytes);
            was.assign(now,now+bytes);
        } else {
            buf_.push_back(1);
            for(std::uint32_t i=0;i<bytes;) {
                if(was[i]==now[i]){++i;continue;}
                std::uint32_t end=i+1;
                // a run ends at 8 bytes alike (a run's own header is 8)
                for(std::uint32_t same=0;end<bytes && same<8;++end)same=was[end]==now[end] ? same+1 : 0;
                while(end>i+1 && was[end-1]==now[end-1])--end;
                Append(i);Append(end-i);buf_.insert(buf_.end(),now+i,now+end);
                std::memcpy(was.data()+i,now+i,end-i);
                i=end;
            }
            Append(std::uint32_t{0});Append(std::uint32_t{0});
        }
        Record(kRegion,buf_.data(),buf_.size());
    }
    void Value(const void* object,const char* name,const void* data,std::size_t size) noexcept {
        buf_.clear();Append(reinterpret_cast<std::uint64_t>(object));Name(name);
        const auto* d=static_cast<const unsigned char*>(data);buf_.insert(buf_.end(),d,d+size);
        Record(kValue,buf_.data(),buf_.size());
    }
    template<class T> void Value(const void* object,const char* name,const T& v) noexcept { Value(object,name,&v,sizeof(T)); }
    void Output(const char* name,const void* data,std::size_t size) noexcept {
        buf_.clear();Name(name);
        const auto* d=static_cast<const unsigned char*>(data);buf_.insert(buf_.end(),d,d+size);
        Record(kOutput,buf_.data(),buf_.size());
    }

private:
    static void Put(unsigned char* b,std::uint64_t v) noexcept { std::memcpy(b,&v,8); }
    template<class T> void Append(T v) {
        const auto* p=reinterpret_cast<const unsigned char*>(&v);buf_.insert(buf_.end(),p,p+sizeof(T));
    }
    void Name(const char* name) {
        const std::size_t n=std::strlen(name)>255 ? 255 : std::strlen(name);
        buf_.push_back(static_cast<unsigned char>(n));buf_.insert(buf_.end(),name,name+n);
    }
    void Record(Type t,const void* data,std::size_t size) noexcept {
        if(!f_)return;
        const auto n=static_cast<std::uint32_t>(size);
        std::fputc(t,f_);std::fwrite(&n,4,1,f_);
        if(n)std::fwrite(data,1,n,f_);
    }
    std::FILE* f_=nullptr;
    std::unordered_map<std::uint64_t,std::vector<unsigned char>> last_;
    std::vector<unsigned char> buf_;
};

// --- Reading (the tests) ---
struct Record {
    Type type;
    std::string name;                // kValue / kOutput
    std::uint64_t object=0;          // kCall / kValue
    std::uint8_t step=0;             // kCall
    std::uint64_t frame=0,ms=0;      // kCall
    std::uint64_t address=0;         // kRegion
    std::vector<unsigned char> data; // kRegion: the region's bytes whole (the changes applied); kValue / kOutput: its bytes
};
struct File {
    Header header{};
    std::vector<Record> records;
    std::string error;   // empty: read whole
    bool cut=false;      // the game ended before the recording closed: read up to its last call whole (that one dropped)
};
inline File Read(const char* path) {
    File out;
    std::FILE* f=nullptr;
    if(fopen_s(&f,path,"rb")!=0 || !f){out.error=std::string("cannot open ")+path;return out;}
    std::vector<unsigned char> all;
    unsigned char chunk[1<<16];
    for(std::size_t n;(n=std::fread(chunk,1,sizeof(chunk),f))>0;)all.insert(all.end(),chunk,chunk+n);
    std::fclose(f);
    if(all.size()<sizeof(kMagic)+sizeof(Header) || std::memcmp(all.data(),kMagic,sizeof(kMagic))){out.error="not a trace";return out;}
    std::memcpy(&out.header,all.data()+sizeof(kMagic),sizeof(Header));
    if(out.header.version!=kVersion){out.error="trace version "+std::to_string(out.header.version);return out;}
    std::unordered_map<std::uint64_t,std::vector<unsigned char>> last;
    std::size_t at=sizeof(kMagic)+sizeof(Header);
    auto bad=[&](const char* why){out.error=std::string(why)+" at byte "+std::to_string(at);};
    // A recording the game's exit cut short (nothing closes it then): its last record half written. What came before is
    // whole; the last call is dropped with it, its outputs perhaps not all written.
    auto cutShort=[&]{
        out.cut=true;
        while(!out.records.empty() && out.records.back().type!=kCall)out.records.pop_back();
        if(!out.records.empty())out.records.pop_back();
        return out;
    };
    while(at<all.size()) {
        if(all.size()-at<5)return cutShort();
        Record r{};
        r.type=static_cast<Type>(all[at]);
        std::uint32_t size;std::memcpy(&size,&all[at+1],4);
        const unsigned char* p=all.data()+at+5;
        if(all.size()-at-5<size)return cutShort();
        const unsigned char* const end=p+size;
        auto u64=[&]{std::uint64_t v=0;if(end-p>=8)std::memcpy(&v,p,8);p+=8;return v;};
        auto u32=[&]{std::uint32_t v=0;if(end-p>=4)std::memcpy(&v,p,4);p+=4;return v;};
        auto name=[&]{const std::size_t n=p<end ? *p++ : 0;std::string s(reinterpret_cast<const char*>(p),n);p+=n;return s;};
        switch(r.type) {
        case kReset: break;
        case kCall: r.step=*p++;r.object=u64();r.frame=u64();r.ms=u64();break;
        case kRegion: {
            r.address=u64();const std::uint32_t bytes=u32();const unsigned char mode=*p++;
            std::vector<unsigned char>& was=last[r.address];
            if(mode==0){if(static_cast<std::size_t>(end-p)<bytes){bad("a cut region");return out;}was.assign(p,p+bytes);}
            else {
                if(was.size()!=bytes){bad("a change to a region never recorded whole");return out;}
                for(;;) {
                    const std::uint32_t off=u32(),len=u32();
                    if(!len)break;
                    if(off+len>bytes || static_cast<std::size_t>(end-p)<len){bad("a change past its region");return out;}
                    std::memcpy(was.data()+off,p,len);p+=len;
                }
            }
            r.data=was;
            break;
        }
        case kValue: r.object=u64();r.name=name();r.data.assign(p,end);break;
        case kOutput: r.name=name();r.data.assign(p,end);break;
        default: bad("an unknown record");return out;
        }
        out.records.push_back(std::move(r));
        at+=5+size;
    }
    return out;
}
}  // namespace edf::trace

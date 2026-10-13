#include "loadout_file.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
void Say(wchar_t* why,std::size_t capacity,const wchar_t* text) noexcept {
    if(why && capacity)wcsncpy_s(why,capacity,text,_TRUNCATE);
}
bool WriteAll(HANDLE file,const std::string& bytes) noexcept {
    std::size_t done=0;
    while(done<bytes.size()) {
        const DWORD chunk=static_cast<DWORD>(bytes.size()-done>(1u<<20) ? (1u<<20) : bytes.size()-done);
        DWORD wrote=0;
        if(!WriteFile(file,bytes.data()+done,chunk,&wrote,nullptr) || !wrote)return false;
        done+=wrote;
    }
    return FlushFileBuffers(file)!=0;
}
// `line`'s value when it is `key`=value, else nullptr.
const char* ValueOf(const std::string& line,const char* key) noexcept {
    const std::size_t n=std::strlen(key);
    return line.size()>n && line.compare(0,n,key)==0 && line[n]=='=' ? line.c_str()+n+1 : nullptr;
}
GenerateState StateOf(const char* v) noexcept {
    if(!std::strcmp(v,"running"))return GenerateState::running;
    if(!std::strcmp(v,"ok"))return GenerateState::ok;
    if(!std::strcmp(v,"failed"))return GenerateState::failed;
    return GenerateState::idle;
}
void ReadField(const std::string& line,GenerateStatus* s) noexcept {
    if(const char* v=ValueOf(line,"state"))s->state=StateOf(v);
    else if(const char* m=ValueOf(line,"made"))s->made=std::atoi(m);
    else if(const char* f=ValueOf(line,"failed"))s->failed=std::atoi(f);
    else if(const char* t=ValueOf(line,"message"))
        MultiByteToWideChar(CP_UTF8,0,t,-1,s->message,static_cast<int>(std::size(s->message))-1);
}
}  // namespace

bool ReadFileBytes(const wchar_t* path,std::string* out,FILETIME* written) noexcept {
    if(written)*written=FILETIME{};
    const HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart<(64ll<<20) && (!written || GetFileTime(file,nullptr,nullptr,written));
    if(ok) {
        out->assign(static_cast<std::size_t>(size.QuadPart),'\0');
        DWORD got=0;
        ok=out->empty() || (ReadFile(file,out->data(),static_cast<DWORD>(out->size()),&got,nullptr) && got==out->size());
    }
    CloseHandle(file);
    return ok;
}

bool ReplaceFileAtomic(const wchar_t* path,const std::string& bytes) noexcept {
    wchar_t temp[MAX_PATH+32];
    if(_snwprintf_s(temp,_TRUNCATE,L"%ls.tmp-%lu",path,GetCurrentProcessId())<0)return false;
    const HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const bool written=WriteAll(file,bytes);
    CloseHandle(file);
    if(written && MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    DeleteFileW(temp);
    return false;
}

GenerateStatus ParseGenerateStatus(const std::string& text) noexcept {
    GenerateStatus s;
    std::size_t at=text.compare(0,3,"\xEF\xBB\xBF")==0 ? 3 : 0;
    while(at<text.size()) {
        std::size_t end=text.find('\n',at);
        if(end==std::string::npos)end=text.size();
        std::string line=text.substr(at,end-at);
        if(!line.empty() && line.back()=='\r')line.pop_back();
        ReadField(line,&s);
        at=end+1;
    }
    return s;
}

bool VariantGenerator::Running() noexcept {
    return process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT;
}

void VariantGenerator::Reap() noexcept {
    if(!process_ || Running())return;
    GetExitCodeProcess(process_,&exitCode_);
    CloseHandle(process_);
    process_=nullptr;
    ran_=true;
}

bool VariantGenerator::Start(const wchar_t* exe,const wchar_t* args,wchar_t* why,std::size_t capacity) noexcept {
    if(Running()){Say(why,capacity,L"生成器已在运行");return false;}
    Reap();
    if(GetFileAttributesW(exe)==INVALID_FILE_ATTRIBUTES){Say(why,capacity,L"找不到生成器（请用安装器重新安装）");return false;}
    std::wstring line=L"\""+std::wstring(exe)+L"\" "+args;   // CreateProcessW may write into its command line
    STARTUPINFOW si{};si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe,line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&si,&pi)) {
        Say(why,capacity,L"生成器没能启动");return false;
    }
    CloseHandle(pi.hThread);
    process_=pi.hProcess;
    FILETIME ended{},kernel{},user{};
    // Read the actual process creation time, even when the child has already written its result and exited.
    // GetSystemTime here would incorrectly classify a very fast child's final state as stale.
    if(!GetProcessTimes(process_,&started_,&ended,&kernel,&user))started_=FILETIME{};
    ran_=false;
    return true;
}

// What a finished (or never started) generator left: a run of ours without a final state failed; so did a `running`
// nobody of ours is still writing (a generator killed with the game).
static void Settle(GenerateStatus* s,bool ran,DWORD exitCode) noexcept {
    if(ran && exitCode && s->state==GenerateState::ok)s->state=GenerateState::idle;
    if(s->state==GenerateState::ok || s->state==GenerateState::failed || (!ran && s->state==GenerateState::idle))return;
    s->state=GenerateState::failed;
    if(!ran)Say(s->message,std::size(s->message),L"上次生成没有完成，请重新生成");
    else if(exitCode==3)Say(s->message,std::size(s->message),L"另一个生成器正在运行");
    else _snwprintf_s(s->message,_TRUNCATE,L"生成器异常退出（退出码 %lu）",exitCode);
}

GenerateStatus VariantGenerator::Status(const wchar_t* statusPath) noexcept {
    const bool running=Running();
    if(!running)Reap();
    std::string text;
    FILETIME written{};
    const bool read=statusPath && ReadFileBytes(statusPath,&text,&written);
    // Read the timestamp on the same handle as the bytes: atomic replacement must not pair an old result with the
    // new file's timestamp. A child that exits before writing running/final must not reuse the last run's result.
    const bool current=(!process_ && !ran_) || ((started_.dwHighDateTime || started_.dwLowDateTime) &&
                                              CompareFileTime(&written,&started_)>=0);
    GenerateStatus s=read && current ? ParseGenerateStatus(text) : GenerateStatus{};
    if(!running) {
        Settle(&s,ran_,exitCode_);
        return s;
    }
    s.state=GenerateState::running;
    if(!s.message[0])Say(s.message,std::size(s.message),L"生成中");
    return s;
}

VariantGenerator::~VariantGenerator() {
    if(process_)CloseHandle(process_);   // the generator keeps running on its own; only the handle goes
}
}  // namespace crew

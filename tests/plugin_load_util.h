// Helpers for the tests that load the built plugin DLLs into their own process (tests/plugin_pair_load_test.cpp,
// tests/profile_native_test.cpp): a scratch directory of the test's own for the copies (EML6_Load writes its .log
// beside the DLL, and must not write into build/Mods/Plugins), the copy loaded from there, the plugin's log waited for
// (EDF6VehicleCrew writes it from a writer thread every kFlushMs, src/plugin.cpp LogWriter) and its export table read.
#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>

namespace plugintest {
inline int checks=0;
inline void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::fflush(stderr);std::exit(1);}
}

// `dir` made and emptied of what an earlier run left (its own files only: the test's scratch directory).
inline void FreshDir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir,ec);
    Check(!ec && std::filesystem::is_directory(dir),"the scratch directory made");
    for(const auto& e:std::filesystem::directory_iterator(dir,ec)) {
        std::error_code rm;
        std::filesystem::remove_all(e.path(),rm);
        if(rm)std::fprintf(stderr,"FAIL: %ls left from an earlier run cannot be removed (%s)\n",e.path().c_str(),rm.message().c_str());
        Check(!rm,"the scratch directory emptied");
    }
}

// `from` copied into `dir` under its own name; the copy's path.
inline std::filesystem::path CopyInto(const std::filesystem::path& from,const std::filesystem::path& dir) {
    const std::filesystem::path to=dir/from.filename();
    if(!CopyFileW(from.c_str(),to.c_str(),FALSE))std::fprintf(stderr,"FAIL: copy %ls -> %ls: error %lu\n",from.c_str(),to.c_str(),GetLastError());
    Check(std::filesystem::exists(to),"the plugin DLL copied into the scratch directory");
    return to;
}

// The copy loaded as the mod loader loads a plugin: its own directory first for what it imports, then the system's.
inline HMODULE LoadPlugin(const std::filesystem::path& dll) {
    const HMODULE m=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!m)std::fprintf(stderr,"FAIL: LoadLibraryExW %ls: error %lu\n",dll.c_str(),GetLastError());
    Check(m!=nullptr,"the plugin DLL loads");
    return m;
}

inline std::string ReadShared(const std::filesystem::path& path) {
    const HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return {};
    std::string text;
    char buffer[4096];
    DWORD got=0;
    while(ReadFile(h,buffer,sizeof(buffer),&got,nullptr) && got)text.append(buffer,got);
    CloseHandle(h);
    return text;
}

// The log's text once it holds `needle` `count` times (polled every 10 ms, up to 5 s: the writer flushes every
// 50 ms); what it holds after the 5 s otherwise.
inline std::string WaitForLog(const std::filesystem::path& log,const std::string& needle,int count=1) {
    std::string text;
    for(int i=0;i<500;++i) {
        text=ReadShared(log);
        int n=0;
        for(std::size_t at=text.find(needle);at!=std::string::npos;at=text.find(needle,at+needle.size()))++n;
        if(n>=count)return text;
        Sleep(10);
    }
    std::fprintf(stderr,"FAIL: %ls: no %d x \"%s\" within 5 s; it holds:\n%s\n",log.c_str(),count,needle.c_str(),text.c_str());
    return text;
}
inline int Count(const std::string& text,const std::string& needle) {
    int n=0;
    for(std::size_t at=text.find(needle);at!=std::string::npos;at=text.find(needle,at+needle.size()))++n;
    return n;
}

// The names in a loaded module's export table.
inline std::set<std::string> ExportNames(HMODULE m) {
    std::set<std::string> names;
    const auto base=reinterpret_cast<const unsigned char*>(m);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dir.VirtualAddress)return names;
    const auto exp=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+dir.VirtualAddress);
    const auto rvas=reinterpret_cast<const DWORD*>(base+exp->AddressOfNames);
    for(DWORD i=0;i<exp->NumberOfNames;++i)names.insert(reinterpret_cast<const char*>(base+rvas[i]));
    return names;
}

// The module `p` lies in (no reference taken), or nullptr.
inline HMODULE OwnerOf(const void* p) {
    HMODULE m=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(p),&m))return nullptr;
    return m;
}
}  // namespace plugintest

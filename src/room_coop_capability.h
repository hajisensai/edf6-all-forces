#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace crew::roomui {
// Read-only image mapping: neither DllMain nor dependencies run, and the DLL is not registered as loaded. A normal
// LoadLibrary/DONT_RESOLVE_DLL_REFERENCES probe would poison the mod loader's later real load of the same module.
inline bool HasDllExport(const wchar_t* path,const char* name) noexcept {
    const HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
                                  OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    const HANDLE map=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr);
    CloseHandle(file);
    if(!map)return false;
    const auto* base=static_cast<const unsigned char*>(MapViewOfFile(map,FILE_MAP_READ,0,0,0));
    bool found=false;
    if(base) {
        __try {
            const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0) {
                const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
                if(nt->Signature==IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                   nt->OptionalHeader.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_EXPORT) {
                    const std::uint32_t size=nt->OptionalHeader.SizeOfImage;
                    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                    if(directory.VirtualAddress && directory.VirtualAddress<size &&
                       sizeof(IMAGE_EXPORT_DIRECTORY)<=size-directory.VirtualAddress) {
                        const auto* exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+directory.VirtualAddress);
                        if(exports->AddressOfNames<size && exports->NumberOfNames<=(size-exports->AddressOfNames)/4) {
                            const auto* names=reinterpret_cast<const std::uint32_t*>(base+exports->AddressOfNames);
                            const std::size_t length=std::strlen(name);
                            for(std::uint32_t i=0;i<exports->NumberOfNames;++i) {
                                const std::uint32_t rva=names[i];
                                if(rva<size && length<size-rva && !std::memcmp(base+rva,name,length+1)){found=true;break;}
                            }
                        }
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){found=false;}
        UnmapViewOfFile(base);
    }
    CloseHandle(map);
    return found;
}
}  // namespace crew::roomui

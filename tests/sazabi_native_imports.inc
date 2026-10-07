bool RuntimeImports(HMODULE module) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for(auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d) {
        const char* dll=reinterpret_cast<const char*>(base+d->Name);
        if(_strnicmp(dll,"VCRUNTIME",9) && _strnicmp(dll,"MSVCP",5) && _strnicmp(dll,"api-ms-win-crt-",15))continue;
        HMODULE runtime=LoadLibraryA(dll);if(!runtime)return false;
        auto names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+d->OriginalFirstThunk);
        auto slots=reinterpret_cast<std::uint64_t*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            FARPROC proc=GetProcAddress(runtime,reinterpret_cast<const char*>(name->Name));
            if(!proc)continue;
            DWORD old=0;if(!VirtualProtect(slots,8,PAGE_READWRITE,&old))return false;
            *slots=reinterpret_cast<std::uint64_t>(proc);VirtualProtect(slots,8,old,&old);
        }
    }
    return true;
}

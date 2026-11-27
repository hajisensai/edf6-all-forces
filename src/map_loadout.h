#pragma once
// The M map's loadout panel (the user, 2026-11-27: 「包括m里面装配东西」; Broken Arrow's loadout customization): the armed
// support's SupportVehicle_<KEY> switched among the variants this machine has installed. The one data set: the value the
// installer's menu 8 and the in-game loadout menu edit (support_loadout.h's grammar, roommenu::SerialiseVehicle), written
// by loadout_store.h SaveSupportLoadout. Nothing new is made here: a variant is a file the installer generated
// (EDF6VC_LO_<BODY>_<16 hex>.SGO in Mods/OBJECT, support_loadout.h VehicleVariantFile), its name decoded back.
// Pure (no game, no Windows): tools/map_buttons_check.cpp checks it.
#include "map_loadout_row.h"
#include "room_menu.h"
#include "support_loadout.h"
#include <cstdint>
#include <cwchar>

namespace crew::maploadout {

// `name` is `body`'s variant file (canonical: VehicleVariantFile of the loadout it decodes to gives it back): *out its
// loadout.
inline bool VariantOfFile(const wchar_t* name,VehicleBody body,VehicleLoadout* out) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(body);
    if(!row || !name)return false;
    wchar_t prefix[40];
    if(std::swprintf(prefix,40,L"EDF6VC_LO_%ls_",row->name)<=0)return false;
    const std::size_t p=std::wcslen(prefix);
    if(std::wcslen(name)!=p+16+4 || _wcsnicmp(name,prefix,p)!=0 || _wcsicmp(name+p+16,L".SGO")!=0)return false;
    std::uint64_t v=0;
    for(std::size_t i=0;i<16;++i) {
        const wchar_t c=name[p+i];
        const int d=c>=L'0' && c<=L'9' ? c-L'0' : c>=L'A' && c<=L'F' ? c-L'A'+10 : c>=L'a' && c<=L'f' ? c-L'a'+10 : -1;
        if(d<0)return false;
        v=(v<<4)|static_cast<std::uint64_t>(d);
    }
    VehicleLoadout l{};
    if(!LoadoutOfVariant(v,body,&l))return false;
    wchar_t again[kFileMost];
    if(!VehicleVariantFile(l,again,kFileMost) || _wcsicmp(again,name)!=0)return false;
    if(out)*out=l;
    return true;
}

// The rows for `body` whose SupportVehicle_<KEY> is `current` (body none: stock): stock first, then `current` when it is
// a loadout, then each other installed variant of the `n` file names `nameOf(i)` gives (in their order), at most `most`.
// `stateOf(file)` says a variant's State (ready / next / peerMissing). Each loadout once. Returns the rows.
template<class NameOf,class StateOf>
int BuildRows(VehicleBody body,const VehicleLoadout& current,int n,NameOf nameOf,StateOf stateOf,Row* out,int most) noexcept {
    if(!out || most<=0 || !VehicleBodyOf(body))return 0;
    int k=0;
    out[k]=Row{};out[k].state=State::stock;out[k].current=!current.On();++k;
    const auto add=[&](const VehicleLoadout& l,bool isCurrent) noexcept {
        if(k>=most)return;
        Row r{};
        if(!roommenu::SerialiseVehicle(l,r.value,kValueMost) || !VehicleVariantFile(l,r.file,kFileMost))return;
        r.state=stateOf(static_cast<const wchar_t*>(r.file));r.current=isCurrent;
        out[k++]=r;
    };
    if(current.On() && current.body==body)add(current,true);
    for(int i=0;i<n && k<most;++i) {
        VehicleLoadout l{};
        if(!VariantOfFile(nameOf(i),body,&l))continue;
        bool listed=false;   // the current row's, or a name listed twice: one row
        for(int j=1;j<k && !listed;++j) {
            VehicleLoadout had{};
            listed=VariantOfFile(out[j].file,body,&had) && roommenu::SameVehicle(had,l);
        }
        if(!listed)add(l,false);
    }
    return k;
}

}  // namespace crew::maploadout

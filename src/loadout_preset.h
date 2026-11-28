// 整备方案 (docs/plan-2026-11-24-ui-rewrite.md, stage 1; the player, 2026-11-24: "这个方案应该可以设置多种方案，方便切换"):
// named loadouts that hold everything the 整备 screen edits, switched as a whole. Pure, no game, no Windows:
// tests/loadout_preset_test.cpp runs it offline.
//
// A preset is
//   - the support families of EDF6VehicleCrew.ini [VehicleCrew] (SupportPreset_<KEY>, SupportVehicle_<KEY> incl. OWN_* and
//     OWN_MECH, SupportCrewColour[_<KEY>]): every such key the preset has, with its value; a key it does not have is the
//     stock load. Switching makes the ini exactly that (SwitchEdits: absent keys removed), each value checked by the
//     plugin's own parsers when loadout_store.h SaveSupportLoadouts writes it;
//   - optionally the player's class and the 6 weapon slots of each of the kClasses classes (the 4 stock ones and the
//     plugin's 4 载具手 / 5 刻印剑士, docs/plan-2026-11-28-new-classes-impl.md §1), by WeaponTable row *name* (a row's
//     index moves with every weapon table install; the name does not). An empty name leaves that slot as it is; the
//     slots a class does not have (载具手 2 of 6, 刻印剑士 3 of 6) stay empty.
// The book (Mods/Plugins/EDF6VehicleCrew.presets.ini, UTF-8, written atomically by the caller) keeps at most kPresetMost
// presets, the current one and which preset each mission was last started with (the map's 本关出动 row):
//   [Book]          Format=<kFormat>   Current=<1-based, 0 none>
//   [Preset<n>]     Name=<UTF-8, 1..kPresetNameMost bytes>   Class=<0..5, absent: player not stored>
//                   Weapons<c>=<name>,<name>,<name>,<name>,<name>,<name>   (c 0..5; a line not there: all 6 empty)
//                   S.<ini key>=<value>                                     (one per support key)
//   [Missions]      <mission key>=<1-based preset>
// Parse refuses the whole file on any malformed line (the caller keeps the book it has and says why); a mission naming a
// preset that does not exist is dropped, not an error (a preset deleted by hand).
//
// Format versions. 1 (no Format line; until 2026-11-28): classes 0..3, Weapons0..3. 2: classes 0..5, Weapons0..5.
// Migrating a version 1 book: it is read as it is (all it can hold is valid in 2; the new classes' lines are absent, so
// their slots are left as they are) and the next write is version 2 (Serialize always writes kFormat). Nothing else
// differs, so there is no version 1 path. A Format other than kFormat (a book of a newer plugin, a hand edit) is refused
// like any malformed line: the caller keeps the book it has.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crew::preset {

inline constexpr int kPresetMost=8;
inline constexpr std::size_t kPresetNameMost=48;   // bytes of UTF-8
inline constexpr int kFormat=2;
// The classes a preset holds: SoldierInit 0..3 stock, 4 载具手 (ZwVehicler), 5 刻印剑士 (ZwRuneBlade); GameStatus keeps
// 10 per player (+0x6E98 + class*0x18), so 0..5 are all real cells.
inline constexpr int kClasses=6,kSlots=6;
inline constexpr std::size_t kMissionsMost=1024;
inline constexpr std::size_t kValueMost=240;       // loadout_ini.h ValidLoadoutValue's limit
inline constexpr std::size_t kTokenMost=64;

// A support key of [VehicleCrew]: which family and unit (loadout_ini.h LoadoutField / LoadoutKey's spelling).
enum class Family : std::uint8_t { preset, vehicle, crew };
struct SupportKey { Family family=Family::preset; std::string unit; };

namespace detail {
inline bool Token(std::string_view s,bool allowEmpty=false) noexcept {
    if(s.empty())return allowEmpty;
    if(s.size()>kTokenMost)return false;
    for(char c:s)
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return false;
    return true;
}
inline bool StartsWith(std::string_view s,std::string_view p) noexcept { return s.size()>=p.size() && s.substr(0,p.size())==p; }
inline std::string_view Trim(std::string_view s) noexcept {
    while(!s.empty() && (s.front()==' ' || s.front()=='\t'))s.remove_prefix(1);
    while(!s.empty() && (s.back()==' ' || s.back()=='\t' || s.back()=='\r'))s.remove_suffix(1);
    return s;
}
inline bool Printable(std::string_view s) noexcept {
    for(char c:s)if(static_cast<unsigned char>(c)<0x20 || static_cast<unsigned char>(c)>0x7E)return false;
    return true;
}
// Valid UTF-8 without control characters (a name the screen draws; '=' and '[' are fine: only the first '=' splits).
inline bool NameText(std::string_view s) noexcept {
    if(s.empty() || s.size()>kPresetNameMost)return false;
    for(std::size_t i=0;i<s.size();) {
        const unsigned char c=static_cast<unsigned char>(s[i]);
        int n=c<0x80 ? 0 : (c&0xE0)==0xC0 ? 1 : (c&0xF0)==0xE0 ? 2 : (c&0xF8)==0xF0 ? 3 : -1;
        if(n<0 || (n==0 && (c<0x20 || c==0x7F)) || (n==1 && c<0xC2))return false;
        if(n>0 && i+static_cast<std::size_t>(n)>=s.size())return false;
        for(int k=1;k<=n;++k)if((static_cast<unsigned char>(s[i+k])&0xC0)!=0x80)return false;
        i+=static_cast<std::size_t>(n)+1;
    }
    return s.front()!=' ' && s.back()!=' ';
}
inline bool ParseInt(std::string_view s,int lo,int hi,int* out) noexcept {
    if(s.empty() || s.size()>4)return false;
    int v=0;
    for(char c:s){if(c<'0' || c>'9')return false;v=v*10+(c-'0');}
    if(v<lo || v>hi)return false;
    *out=v;
    return true;
}
}  // namespace detail

// `key` as a support key of the three families ("SupportPreset_SQUAD", "SupportCrewColour"); false for any other key.
inline bool ParseSupportKey(std::string_view key,SupportKey* out) {
    using detail::StartsWith;
    struct P { std::string_view prefix; Family f; };
    static constexpr P kPrefixes[]={{"SupportPreset_",Family::preset},{"SupportVehicle_",Family::vehicle},
                                    {"SupportCrewColour_",Family::crew}};
    if(key=="SupportCrewColour"){*out=SupportKey{Family::crew,{}};return true;}
    for(const P& p:kPrefixes)
        if(StartsWith(key,p.prefix) && detail::Token(key.substr(p.prefix.size()))) {
            *out=SupportKey{p.f,std::string(key.substr(p.prefix.size()))};
            return true;
        }
    return false;
}
inline bool IsSupportKey(std::string_view key) { SupportKey k; return ParseSupportKey(key,&k); }

struct Entry {
    std::string key,value;
    bool operator==(const Entry& o) const { return key==o.key && value==o.value; }
};
struct Player {
    int cls=-1;                                     // -1: the preset does not hold the player's loadout
    std::string weapon[kClasses][kSlots];           // WeaponTable row names; empty: that slot left as it is
    bool operator==(const Player& o) const {
        if(cls!=o.cls)return false;
        for(int c=0;c<kClasses;++c)for(int s=0;s<kSlots;++s)if(weapon[c][s]!=o.weapon[c][s])return false;
        return true;
    }
};
struct Preset {
    std::string name;
    std::vector<Entry> support;   // sorted by key, no key twice, every value non-empty
    Player player;
};
struct Book {
    std::vector<Preset> presets;
    int current=-1;                                         // index; -1 none
    std::vector<std::pair<std::string,int>> missions;       // normalized mission key -> index, sorted by key
};

// A mission key as the map's table compares it (worldmap: case-insensitive, '\' and '/' alike): lower case, '/'.
inline std::string MissionKey(std::string_view key) {
    std::string out(detail::Trim(key));
    for(char& c:out){if(c=='\\')c='/';else if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');}
    return out;
}
inline bool ValidMissionKey(std::string_view key) noexcept {
    if(key.empty() || key.size()>128 || !detail::Printable(key))return false;
    return key.find('=')==std::string_view::npos && key.front()!='[' && key.front()!=';';
}

// Every support key of `ini`'s [VehicleCrew] with a non-empty value, sorted by key. A key written twice by hand: the
// first, the one GetPrivateProfileStringW (the plugin's reader) returns; the menu's writer never leaves two.
inline std::vector<Entry> CaptureSupport(std::string_view ini) {
    std::vector<Entry> out;
    bool in=false;
    std::size_t at=0;
    if(ini.size()>=3 && ini.substr(0,3)=="\xEF\xBB\xBF")at=3;
    while(at<ini.size()) {
        std::size_t end=ini.find('\n',at);
        if(end==std::string_view::npos)end=ini.size();
        const std::string_view line=detail::Trim(ini.substr(at,end-at));
        at=end+1;
        if(line.empty() || line.front()==';' || line.front()=='#')continue;
        if(line.front()=='[') {
            const std::size_t close=line.find(']');
            std::string name(close==std::string_view::npos ? line.substr(1) : line.substr(1,close-1));
            for(char& c:name)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
            in=detail::Trim(name)=="vehiclecrew";
            continue;
        }
        if(!in)continue;
        const std::size_t eq=line.find('=');
        if(eq==std::string_view::npos)continue;
        const std::string_view key=detail::Trim(line.substr(0,eq)),value=detail::Trim(line.substr(eq+1));
        if(value.empty() || !IsSupportKey(key))continue;
        const bool seen=std::any_of(out.begin(),out.end(),[&](const Entry& e){return e.key==key;});
        if(!seen)out.push_back(Entry{std::string(key),std::string(value)});
    }
    std::sort(out.begin(),out.end(),[](const Entry& a,const Entry& b){return a.key<b.key;});
    return out;
}

// What switching to `to` writes into an ini whose support keys are now `now` (CaptureSupport): every key whose value
// differs, an empty value for a key `to` does not have (removed: the stock load). Sorted by key; empty: nothing to do.
inline std::vector<Entry> SwitchEdits(const std::vector<Entry>& now,const std::vector<Entry>& to) {
    std::vector<Entry> out;
    std::size_t i=0,j=0;
    while(i<now.size() || j<to.size()) {
        if(j==to.size() || (i<now.size() && now[i].key<to[j].key)){out.push_back(Entry{now[i].key,{}});++i;}
        else if(i==now.size() || to[j].key<now[i].key){out.push_back(to[j]);++j;}
        else {
            if(now[i].value!=to[j].value)out.push_back(to[j]);
            ++i;++j;
        }
    }
    return out;
}

// --- The book's file ---

inline bool ValidPreset(const Preset& p,std::string* why) {
    auto no=[&](const char* w){if(why)*why=w;return false;};
    if(!detail::NameText(p.name))return no("方案名无效（1–48 字节，不能有控制字符、首尾空格）");
    for(std::size_t i=0;i<p.support.size();++i) {
        const Entry& e=p.support[i];
        if(!IsSupportKey(e.key))return no("方案里有不是支援配置的键");
        if(e.value.empty() || e.value.size()>kValueMost || !detail::Printable(e.value))return no("支援配置的值无效");
        if(i && !(p.support[i-1].key<e.key))return no("支援配置的键重复或没排序");
    }
    if(p.player.cls<-1 || p.player.cls>=kClasses)return no("兵种无效");
    for(int c=0;c<kClasses;++c)
        for(int s=0;s<kSlots;++s) {
            const std::string& w=p.player.weapon[c][s];
            if(!detail::Token(w,true))return no("武器名无效");
            if(p.player.cls<0 && !w.empty())return no("没存兵种却存了武器");
        }
    return true;
}

inline std::string Serialize(const Book& b) {
    std::string out="; EDF6 全军出击 整备方案（游戏内整备界面写入；手改请保持格式）\r\n[Book]\r\nFormat=";
    out+=std::to_string(kFormat);
    out+="\r\nCurrent=";
    out+=std::to_string(b.current<0 ? 0 : b.current+1);
    out+="\r\n";
    for(std::size_t i=0;i<b.presets.size();++i) {
        const Preset& p=b.presets[i];
        out+="\r\n[Preset"+std::to_string(i+1)+"]\r\nName="+p.name+"\r\n";
        if(p.player.cls>=0) {
            out+="Class="+std::to_string(p.player.cls)+"\r\n";
            for(int c=0;c<kClasses;++c) {
                out+="Weapons"+std::to_string(c)+"=";
                for(int s=0;s<kSlots;++s){if(s)out+=',';out+=p.player.weapon[c][s];}
                out+="\r\n";
            }
        }
        for(const Entry& e:p.support)out+="S."+e.key+"="+e.value+"\r\n";
    }
    if(!b.missions.empty()) {
        out+="\r\n[Missions]\r\n";
        for(const auto& m:b.missions)out+=m.first+"="+std::to_string(m.second+1)+"\r\n";
    }
    return out;
}

// The book of `text`; false (and `why`): *out untouched. An empty text is an empty book.
inline bool Parse(std::string_view text,Book* out,std::string* why) {
    auto no=[&](const std::string& w){if(why)*why=w;return false;};
    Book b;
    int current=0;
    bool formatSeen=false;   // no Format line: version 1, read as it is (see the top)
    std::vector<std::pair<std::string,int>> missions;
    enum class Sec { none, book, preset, missions } sec=Sec::none;
    Preset* p=nullptr;
    std::vector<bool> classSeen;   // per preset: Class= given
    std::size_t at=text.size()>=3 && text.substr(0,3)=="\xEF\xBB\xBF" ? 3 : 0;
    int lineNo=0;
    while(at<text.size()) {
        std::size_t end=text.find('\n',at);
        if(end==std::string_view::npos)end=text.size();
        const std::string_view line=detail::Trim(text.substr(at,end-at));
        at=end+1;
        ++lineNo;
        const std::string where="第 "+std::to_string(lineNo)+" 行：";
        if(line.empty() || line.front()==';')continue;
        if(line.front()=='[') {
            if(line.back()!=']')return no(where+"节名没有 ]");
            const std::string_view name=line.substr(1,line.size()-2);
            if(name=="Book")sec=Sec::book;
            else if(name=="Missions")sec=Sec::missions;
            else if(detail::StartsWith(name,"Preset")) {
                int n=0;
                if(!detail::ParseInt(name.substr(6),1,kPresetMost,&n))return no(where+"方案编号无效（1–8）");
                if(n!=static_cast<int>(b.presets.size())+1)return no(where+"方案编号要从 1 起连续");
                b.presets.emplace_back();
                classSeen.push_back(false);
                p=&b.presets.back();
                sec=Sec::preset;
            }
            else return no(where+"不认识的节");
            continue;
        }
        const std::size_t eq=line.find('=');
        if(eq==std::string_view::npos)return no(where+"没有 =");
        const std::string_view key=detail::Trim(line.substr(0,eq)),value=detail::Trim(line.substr(eq+1));
        switch(sec) {
        case Sec::none: return no(where+"在任何节之前");
        case Sec::book:
            if(key=="Format") {
                int format=0;
                if(formatSeen || !detail::ParseInt(value,1,9999,&format) || format!=kFormat)
                    return no(where+"方案文件格式版本不是 "+std::to_string(kFormat)+"（更新的插件写的？）");
                formatSeen=true;
            }
            else if(key!="Current" || !detail::ParseInt(value,0,kPresetMost,&current))
                return no(where+"[Book] 只认 Format="+std::to_string(kFormat)+" 和 Current=0–8");
            break;
        case Sec::missions: {
            int n=0;
            if(!ValidMissionKey(key) || !detail::ParseInt(value,1,kPresetMost,&n))return no(where+"任务记忆无效");
            if(missions.size()>=kMissionsMost)return no(where+"任务记忆太多");
            missions.emplace_back(MissionKey(key),n-1);
            break;
        }
        case Sec::preset:
            if(key=="Name"){if(!p->name.empty())return no(where+"Name 重复");p->name=std::string(value);}
            else if(key=="Class") {
                if(classSeen.back() || !detail::ParseInt(value,0,kClasses-1,&p->player.cls))return no(where+"Class 无效");
                classSeen.back()=true;
            }
            else if(detail::StartsWith(key,"Weapons") && key.size()==8 && key[7]>='0' && key[7]<'0'+kClasses) {
                const int c=key[7]-'0';
                std::size_t from=0;
                for(int s=0;s<kSlots;++s) {
                    const std::size_t comma=value.find(',',from);
                    if((comma==std::string_view::npos)!=(s==kSlots-1))return no(where+"武器要正好 6 格");
                    p->player.weapon[c][s]=std::string(detail::Trim(value.substr(from,comma==std::string_view::npos ? value.npos : comma-from)));
                    from=comma+1;
                }
            }
            else if(detail::StartsWith(key,"S.")) {
                const std::string k(key.substr(2));
                if(std::any_of(p->support.begin(),p->support.end(),[&](const Entry& e){return e.key==k;}))return no(where+"支援键重复");
                p->support.push_back(Entry{k,std::string(value)});
            }
            else return no(where+"方案里不认识的键");
            break;
        }
    }
    for(std::size_t i=0;i<b.presets.size();++i) {
        Preset& q=b.presets[i];
        std::sort(q.support.begin(),q.support.end(),[](const Entry& x,const Entry& y){return x.key<y.key;});
        std::string w;
        if(!ValidPreset(q,&w))return no("方案 "+std::to_string(i+1)+"："+w);
    }
    if(current>static_cast<int>(b.presets.size()))return no("Current 指向不存在的方案");
    b.current=current-1;
    std::sort(missions.begin(),missions.end());
    for(const auto& m:missions) {
        if(m.second>=static_cast<int>(b.presets.size()))continue;
        if(!b.missions.empty() && b.missions.back().first==m.first)return no("任务记忆重复："+m.first);
        b.missions.push_back(m);
    }
    *out=std::move(b);
    return true;
}

// --- Editing the book (each keeps it valid; false: refused, the book unchanged) ---

inline int MissionPreset(const Book& b,std::string_view mission) {
    const std::string k=MissionKey(mission);
    auto it=std::lower_bound(b.missions.begin(),b.missions.end(),k,[](const auto& m,const std::string& x){return m.first<x;});
    return it!=b.missions.end() && it->first==k ? it->second : -1;
}
inline bool RememberMission(Book& b,std::string_view mission,int index) {
    if(!ValidMissionKey(detail::Trim(mission)) || index<0 || index>=static_cast<int>(b.presets.size()))return false;
    const std::string k=MissionKey(mission);
    auto it=std::lower_bound(b.missions.begin(),b.missions.end(),k,[](const auto& m,const std::string& x){return m.first<x;});
    if(it!=b.missions.end() && it->first==k){it->second=index;return true;}
    if(b.missions.size()>=kMissionsMost)return false;
    b.missions.insert(it,{k,index});
    return true;
}
// A new preset `name` holding `support` (CaptureSupport) and `loadout`, appended; its index, -1 when the book is full or
// the preset invalid.
inline int AddPreset(Book& b,const std::string& name,std::vector<Entry> support,const Player& loadout) {
    if(static_cast<int>(b.presets.size())>=kPresetMost)return -1;
    std::sort(support.begin(),support.end(),[](const Entry& x,const Entry& y){return x.key<y.key;});
    Preset p{name,std::move(support),loadout};
    if(!ValidPreset(p,nullptr))return -1;
    b.presets.push_back(std::move(p));
    return static_cast<int>(b.presets.size())-1;
}
inline bool CopyPreset(Book& b,int index,const std::string& name) {
    if(index<0 || index>=static_cast<int>(b.presets.size()))return false;
    const Preset src=b.presets[static_cast<std::size_t>(index)];
    return AddPreset(b,name,src.support,src.player)>=0;
}
inline bool RenamePreset(Book& b,int index,const std::string& name) {
    if(index<0 || index>=static_cast<int>(b.presets.size()) || !detail::NameText(name))return false;
    b.presets[static_cast<std::size_t>(index)].name=name;
    return true;
}
// The preset's contents replaced by what is in force now (the screen's R: 存入方案).
inline bool StorePreset(Book& b,int index,std::vector<Entry> support,const Player& loadout) {
    if(index<0 || index>=static_cast<int>(b.presets.size()))return false;
    std::sort(support.begin(),support.end(),[](const Entry& x,const Entry& y){return x.key<y.key;});
    Preset p{b.presets[static_cast<std::size_t>(index)].name,std::move(support),loadout};
    if(!ValidPreset(p,nullptr))return false;
    b.presets[static_cast<std::size_t>(index)]=std::move(p);
    return true;
}
// Deleted: the presets after it move up, missions remembering it are forgotten, the others follow their preset.
inline bool DeletePreset(Book& b,int index) {
    if(index<0 || index>=static_cast<int>(b.presets.size()))return false;
    b.presets.erase(b.presets.begin()+index);
    std::vector<std::pair<std::string,int>> kept;
    for(auto& m:b.missions) {
        if(m.second==index)continue;
        kept.emplace_back(m.first,m.second>index ? m.second-1 : m.second);
    }
    b.missions.swap(kept);
    if(b.current==index)b.current=-1;
    else if(b.current>index)--b.current;
    return true;
}

}  // namespace crew::preset

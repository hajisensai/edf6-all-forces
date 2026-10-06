// The HUD's words in each language (docs/hud-re.md §11): every text the plugin's own HUD shows is a key of
// src/hudtext.inc with its English, Simplified Chinese, Traditional Chinese and Japanese, and the code asks for it by
// key (Tr). The language follows the game's own text language (Option_Language: the value EDF.dll keeps at
// EDF+0x20B2B30, hud.cpp GameTextLanguage) unless the ini's HudLanguage names one.
//
// A text is a printf format: every language's must take the same arguments in the same order as the English one
// (tools/hudtext_check.cpp checks it, and that no language is missing a text). The words the HUD gets at run time as
// identifiers (a jet's role, a round's class label, a carrier part's name: logged in English) go through Word, which
// gives the language's word for an identifier the table knows and nullptr for one it does not (shown as it is).
//
// What is translated: states, warnings, prompts, panel and legend labels, the instruments' names. What is not:
// weapon and ammunition designations (AIM-9X, Mk 82, Hydra 70), vehicle class names, key and button names (the
// keyboard's, as Windows names them; the pad's LB / RT / A ...), units (m, km, km/h, s, %) and the g symbol (G).
#pragma once
#include <atomic>
#include <cstring>
#include <cwchar>

namespace hudtext {

enum class Lang : int { en, zhCN, zhTW, ja };
constexpr int kLangs=4;

enum class Tx : int {
#define HUDTEXT(key,en,zhCN,zhTW,ja) key,
#include "hudtext.inc"
#undef HUDTEXT
    count
};
constexpr int kTexts=static_cast<int>(Tx::count);

struct Entry { const char* key; const wchar_t* text[kLangs]; };
inline constexpr Entry kTable[kTexts]={
#define HUDTEXT(key,en,zhCN,zhTW,ja) {#key,{en,zhCN,zhTW,ja}},
#include "hudtext.inc"
#undef HUDTEXT
};

// The language the HUD draws in now (Use; read by any thread).
inline std::atomic<int> current{static_cast<int>(Lang::en)};
inline void Use(Lang l) noexcept { current.store(static_cast<int>(l),std::memory_order_relaxed); }
inline Lang InUse() noexcept { return static_cast<Lang>(current.load(std::memory_order_relaxed)); }

inline const wchar_t* Tr(Tx k,Lang l) noexcept {
    const int i=static_cast<int>(k),n=static_cast<int>(l);
    if(i<0 || i>=kTexts)return L"";
    const wchar_t* t=n>=0 && n<kLangs ? kTable[i].text[n] : nullptr;
    return t ? t : kTable[i].text[0];
}
inline const wchar_t* Tr(Tx k) noexcept { return Tr(k,InUse()); }

// The run-time identifiers the HUD shows (see the top) and their texts.
struct WordEntry { const char* id; Tx text; };
inline constexpr WordEntry kWords[]={
    // a round's class (rounds.cpp kClasses, vhud.cpp ArmOf)
    {"GUN",Tx::wordGun},{"CANNON",Tx::wordCannon},{"MSL",Tx::wordMissile},{"RKT",Tx::wordRocket},{"GREN",Tx::wordGrenade},
    {"LASER",Tx::wordLaser},{"HLASER",Tx::wordHomingLaser},{"BEAM",Tx::wordBeam},{"FLAME",Tx::wordFlame},{"ACID",Tx::wordAcid},
    {"NAPALM",Tx::wordNapalm},{"WPN",Tx::wordWeapon},{"ROCKETS",Tx::wordRocketPods},
    // a plugin unit's kind (jet_internal.h kKinds, hud.cpp HudSee, ground.cpp, heli.cpp kHeliTypes)
    {"strike",Tx::kindStrike},{"fighter",Tx::kindFighter},{"interceptor",Tx::kindInterceptor},{"multirole",Tx::kindMultirole},
    {"carrier",Tx::kindCarrier},{"drone",Tx::kindDrone},{"blast",Tx::kindBlast},{"doll",Tx::kindDoll},{"gunship",Tx::kindGunship},
    {"primer",Tx::kindPrimer},{"centipede",Tx::kindCentipede},{"dragonfly",Tx::kindDragonfly},{"jet",Tx::kindJet},
    {"heli",Tx::kindHeli},{"base",Tx::kindHeliBase},{"crawler",Tx::kindCrawler},{"CRAWLER",Tx::unitCrawler},
    {"drill",Tx::kindDrill},{"npc",Tx::kindNpc},
    // a submarine carrier's deck part (subcarrier.cpp kSystems)
    {"turretA",Tx::partTurretA},{"turretB",Tx::partTurretB},{"missiles",Tx::partMissiles},{"dronebay",Tx::partDroneBay},
};
inline const wchar_t* Word(const char* id,Lang l) noexcept {
    if(!id)return nullptr;
    for(const WordEntry& w:kWords)if(std::strcmp(w.id,id)==0)return Tr(w.text,l);
    return nullptr;
}
inline const wchar_t* Word(const char* id) noexcept { return Word(id,InUse()); }
// `id`'s word in `out` (wide): the language's, else the identifier as it is ("?" for none).
inline void WordTo(const char* id,wchar_t* out,std::size_t size) noexcept {
    if(!size)return;
    const wchar_t* w=Word(id);
    if(w){wcsncpy_s(out,size,w,_TRUNCATE);return;}
    std::size_t n=0;
    for(const char* p=id ? id : "?";*p && n+1<size;++p)out[n++]=static_cast<wchar_t>(static_cast<unsigned char>(*p));
    out[n]=L'\0';
}

// The ini's HudLanguage: auto (the game's) or one of the languages.
enum class Setting : int { automatic, en, zhCN, zhTW, ja };
// "auto", "en", "zh-CN", "zh-TW", "ja" (any case; "zh" alone is zh-CN); -1 for anything else.
inline int ParseSetting(const wchar_t* s) noexcept {
    struct Name { const wchar_t* name; Setting value; };
    static const Name kNames[]={{L"auto",Setting::automatic},{L"",Setting::automatic},{L"en",Setting::en},{L"zh-CN",Setting::zhCN},
                                {L"zh",Setting::zhCN},{L"zh-Hans",Setting::zhCN},{L"zh-TW",Setting::zhTW},{L"zh-Hant",Setting::zhTW},
                                {L"ja",Setting::ja}};
    if(!s)return static_cast<int>(Setting::automatic);
    for(const Name& n:kNames)if(_wcsicmp(n.name,s)==0)return static_cast<int>(n.value);
    return -1;
}
// The game's text language (EDF.dll's Option_Language index: 0 ja, 1 en, 2 kr, 3 cn = Traditional, 4 sc = Simplified;
// the Steam language names map so in 0x706EF0) as the HUD's; Korean has no table of its own: English. -1 (not read): English.
inline Lang FromGame(int game) noexcept {
    switch(game) {
    case 0: return Lang::ja;
    case 3: return Lang::zhTW;
    case 4: return Lang::zhCN;
    default: return Lang::en;
    }
}
inline Lang Resolve(int setting,int game) noexcept {
    switch(static_cast<Setting>(setting)) {
    case Setting::en: return Lang::en;
    case Setting::zhCN: return Lang::zhCN;
    case Setting::zhTW: return Lang::zhTW;
    case Setting::ja: return Lang::ja;
    default: return FromGame(game);
    }
}
inline const char* Name(Lang l) noexcept {
    switch(l) {
    case Lang::zhCN: return "zh-CN";
    case Lang::zhTW: return "zh-TW";
    case Lang::ja: return "ja";
    default: return "en";
    }
}

}  // namespace hudtext

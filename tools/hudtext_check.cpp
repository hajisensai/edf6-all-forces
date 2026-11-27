// The HUD's string table (src/hudtext.h, src/hudtext.inc) checked offline: every text has every language, each
// language's printf format takes the English one's arguments in its order (a text drawn with a wrong argument list
// reads past the arguments: a crash or garbage, never a compile error), the run-time words map to their texts, and
// the language the HUD picks from the game's Option_Language and the ini's HudLanguage. Exit code 1 on a failure.
//
//   hudtext_check
#include "../src/hudtext.h"
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

using namespace hudtext;

namespace {
int failed=0;
void Fail(const char* what,const char* key,int lang) {
    std::printf("FAIL %s: %s [%s]\n",what,key,lang>=0 ? Name(static_cast<Lang>(lang)) : "-");
    ++failed;
}

// A format's conversions in order, each as its length modifier and conversion ("d", "ls", "hs", ".1f" counts as "f";
// "%%" is none). A malformed one ('%' at the end, an unknown conversion) makes it "?".
std::vector<std::wstring> Conversions(const wchar_t* f) {
    std::vector<std::wstring> out;
    for(const wchar_t* p=f;*p;++p) {
        if(*p!=L'%')continue;
        ++p;
        if(*p==L'%')continue;
        while(*p && std::wcschr(L"-+ #0",*p))++p;
        while(*p>=L'0' && *p<=L'9')++p;
        if(*p==L'.'){++p;while(*p>=L'0' && *p<=L'9')++p;}
        std::wstring c;
        if(*p==L'h' || *p==L'l'){c+=*p;++p;}
        if(!*p || !std::wcschr(L"diuxXfFeEgGcsp",*p)){out.push_back(L"?");if(!*p)break;continue;}
        c+=*p;
        out.push_back(c);
    }
    return out;
}

bool Ascii(const wchar_t* s) {
    for(;*s;++s)if(*s>0x7E || (*s<0x20 && *s!=L'\n'))return false;
    return true;
}
bool HasWide(const wchar_t* s) {
    for(;*s;++s)if(*s>=0x3000)return true;
    return false;
}

void Table() {
    for(int i=0;i<kTexts;++i) {
        const Entry& e=kTable[i];
        for(int l=0;l<kLangs;++l)if(!e.text[l] || !e.text[l][0])Fail("missing text",e.key,l);
        if(!e.text[0])continue;
        if(!Ascii(e.text[0]))Fail("English with a non-ASCII character",e.key,0);
        const auto want=Conversions(e.text[0]);
        for(const auto& c:want)if(c==L"?")Fail("malformed format",e.key,0);
        for(int l=1;l<kLangs;++l) {
            if(!e.text[l])continue;
            if(Conversions(e.text[l])!=want)Fail("arguments differ from the English",e.key,l);
        }
        // A text with words in it is translated: the same as the English only where it is a name, a unit or a symbol.
        for(int l=1;l<kLangs;++l)
            if(e.text[l] && std::wcscmp(e.text[l],e.text[0])==0 && !HasWide(e.text[0]) && std::string(e.key)!="holderNpc" &&
               std::string(e.key)!="kindNpc")
                Fail("not translated",e.key,l);
    }
    std::printf("table: %d texts x %d languages\n",kTexts,kLangs);
}

void Words() {
    for(const WordEntry& w:kWords) {
        if(Word(w.id,Lang::en)==nullptr)Fail("word without a text",w.id,-1);
        for(int l=0;l<kLangs;++l)if(Word(w.id,static_cast<Lang>(l))!=Tr(w.text,static_cast<Lang>(l)))Fail("word maps elsewhere",w.id,l);
    }
    if(Word("AIM-9X",Lang::zhCN)!=nullptr)Fail("a designation translated","AIM-9X",1);
    if(Word(nullptr,Lang::zhCN)!=nullptr)Fail("null word","(null)",1);
    wchar_t out[24];
    Use(Lang::zhCN);
    WordTo("GUN",out,24);
    if(std::wcscmp(out,L"机炮")!=0)Fail("WordTo GUN","GUN",1);
    WordTo("Mk 82",out,24);
    if(std::wcscmp(out,L"Mk 82")!=0)Fail("WordTo keeps an unknown identifier","Mk 82",1);
    WordTo(nullptr,out,24);
    if(std::wcscmp(out,L"?")!=0)Fail("WordTo null","(null)",1);
    WordTo("a long identifier that does not fit",out,8);
    if(std::wcslen(out)!=7)Fail("WordTo truncates","(long)",1);
    Use(Lang::en);
    std::printf("words: %zu\n",sizeof(kWords)/sizeof(kWords[0]));
}

void Languages() {
    struct Case { int setting,game; Lang want; const char* what; };
    const Case cases[]={
        {0,0,Lang::ja,"auto, game ja"},{0,1,Lang::en,"auto, game en"},{0,2,Lang::en,"auto, game kr: English"},
        {0,3,Lang::zhTW,"auto, game cn (Traditional)"},{0,4,Lang::zhCN,"auto, game sc (Simplified)"},
        {0,-1,Lang::en,"auto, game not read"},{0,7,Lang::en,"auto, game out of range"},
        {1,4,Lang::en,"en over sc"},{2,0,Lang::zhCN,"zh-CN over ja"},{3,1,Lang::zhTW,"zh-TW over en"},{4,4,Lang::ja,"ja over sc"},
    };
    for(const Case& c:cases)if(Resolve(c.setting,c.game)!=c.want)Fail("language",c.what,static_cast<int>(c.want));
    struct Parse { const wchar_t* text; int want; };
    const Parse parses[]={{L"auto",0},{L"AUTO",0},{L"",0},{L"en",1},{L"EN",1},{L"zh-CN",2},{L"zh-cn",2},{L"zh",2},{L"zh-TW",3},
                          {L"zh-Hant",3},{L"ja",4},{L"jp",-1},{L"chinese",-1},{L"zh-HK",-1}};
    for(const Parse& p:parses)if(ParseSetting(p.text)!=p.want){
        char narrow[16];std::snprintf(narrow,sizeof(narrow),"%ls",p.text);Fail("HudLanguage parse",narrow,-1);
    }
    std::printf("languages: %zu resolve cases, %zu ini spellings\n",sizeof(cases)/sizeof(cases[0]),sizeof(parses)/sizeof(parses[0]));
}
}  // namespace

// The texts drawn only as the optional tail of another (its last %ls, empty when there is nothing to add): each must
// begin with its own separator, or the two run together ("3 UNIT(S)1 CANNOT"). Checked on the composed result.
void Tails() {
    for(int l=0;l<kLangs;++l) {
        const Lang lang=static_cast<Lang>(l);
        wchar_t tail[40]{},line[160]{};
        std::swprintf(tail,_countof(tail),Tr(Tx::cmdCannot,lang),1);
        std::swprintf(line,_countof(line),Tr(Tx::cmdOrderResult,lang),L"X",3,tail);
        const std::wstring all=line;
        const auto at=all.rfind(tail);
        if(at==std::wstring::npos || at==0)Fail("tail not at the end of its result","cmdCannot",l);
        else if(std::iswalnum(all[at-1]) && std::iswalnum(tail[0]))Fail("tail runs into its result with no separator","cmdCannot",l);
        else if(std::iswalnum(tail[0]) || tail[0]==L' ')Fail("tail does not begin with its separator","cmdCannot",l);
    }
}

int main() {
    Table();
    Words();
    Languages();
    Tails();
    std::printf(failed ? "hudtext: %d FAILED\n" : "hudtext: all ok\n",failed);
    return failed ? 1 : 0;
}

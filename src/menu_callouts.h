// The points the armory's vehicle callouts point at (docs/unified-model-re.md section 3.1, docs/room-3d-preview-re.md
// section 6.3). Pure.
//
// The generator (pylib/menu_graft.py) writes them beside each loaded vehicle's merged menu archive:
// Mods\OBJECT\EDF6VC_LO_<body>_<hex>_CALLOUTS.TXT, ASCII, CRLF lines, the header then `tag x y z`: model space, bind
// pose (the same space the preview's model matrix places, preview_camera.h Place), metres. Tags: weapon0..weapon3 (a
// mech's slots in SLOTS order: the weapon's middle, an empty slot its row bone), frame / core (the mech's waist / chest
// bone), pylon0..pylon<n-1> (a jet's stores, in pylon order). The plugin projects them through the matrix the view drew
// the model with (room_preview.h PreviewPlacement) and lays the callout boxes out beside them (support_pages.h).
#pragma once
#include "preview_camera.h"
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace crew::menucallouts {

inline constexpr char kCalloutsHeader[] = "# EDF6VC callouts 1";
inline constexpr wchar_t kCalloutsSuffix[] = L"_CALLOUTS.TXT";
inline constexpr int kMechWeapons=4;
inline constexpr int kPylonCallouts = 7;   // support_loadout.h kPylonsMost (tests/menu_graft_test.py)
inline constexpr int kWeapon0=0,kFrame=kMechWeapons,kCore=kMechWeapons+1,kPylon0=kMechWeapons+2,
    kCalloutTags=kPylon0+kPylonCallouts;
inline constexpr std::size_t kFileMost=4096;   // a full file is ~400 bytes

struct Callouts {
    float p[kCalloutTags][3]{};
    bool have[kCalloutTags]{};
};

// The tag's index (-1: not one).
inline int TagIndex(const char* s,std::size_t n) noexcept {
    const auto is=[&](const char* t) noexcept { return std::strlen(t)==n && std::memcmp(s,t,n)==0; };
    if(is("frame"))return kFrame;
    if(is("core"))return kCore;
    const auto numbered=[&](const char* stem,int count,int base) noexcept {
        const std::size_t k=std::strlen(stem);
        if(n!=k+1 || std::memcmp(s,stem,k)!=0 || s[k]<'0' || s[k]>'9')return -1;
        const int i=s[k]-'0';
        return i<count ? base+i : -1;
    };
    const int w=numbered("weapon",kMechWeapons,kWeapon0);
    return w>=0 ? w : numbered("pylon",kPylonCallouts,kPylon0);
}

// One `tag x y z` line (no line end); false: not one.
inline bool ParseLine(const char* s,std::size_t n,Callouts* out) noexcept {
    char line[128];
    if(n==0 || n>=sizeof(line))return false;
    std::memcpy(line,s,n);
    line[n]=0;
    const char* sp=std::strchr(line,' ');
    if(!sp)return false;
    const int tag=TagIndex(line,static_cast<std::size_t>(sp-line));
    if(tag<0 || out->have[tag])return false;
    const char* at=sp;
    float v[3];
    for(float& x:v) {
        if(*at!=' ' || at[1]==' ' || at[1]==0)return false;
        char* end=nullptr;
        x=std::strtof(at+1,&end);
        if(end==at+1 || !std::isfinite(x))return false;
        at=end;
    }
    if(*at!=0)return false;
    for(int j=0;j<3;++j)out->p[tag][j]=v[j];
    out->have[tag]=true;
    return true;
}

// The file's bytes into `out` (cleared first); false: not a callouts file (a wrong header, an unknown or repeated tag, a
// line that is not `tag x y z`): the caller places no callout on the model.
inline bool Parse(const char* text,std::size_t n,Callouts* out) noexcept {
    *out=Callouts{};
    if(n>kFileMost)return false;
    std::size_t at=0;
    bool header=false;
    while(at<n) {
        std::size_t end=at;
        while(end<n && text[end]!='\r' && text[end]!='\n')++end;
        if(end+1>=n || text[end]!='\r' || text[end+1]!='\n')return false;   // CRLF, the last line too
        const std::size_t len=end-at;
        if(!header) {
            if(len!=sizeof(kCalloutsHeader)-1 || std::memcmp(text+at,kCalloutsHeader,len)!=0)return false;
            header=true;
        } else if(len && !ParseLine(text+at,len,out))return false;
        at=end+2;
    }
    return header;
}

// The callouts file of unified file `sgo` (OBJECT\EDF6VC_LO_X.SGO -> OBJECT\EDF6VC_LO_X_CALLOUTS.TXT); false: not a .SGO
// or `out` too small.
inline bool FileOf(const wchar_t* sgo,wchar_t* out,std::size_t cap) noexcept {
    const std::size_t n=sgo ? std::wcslen(sgo) : 0;
    const std::size_t suffix=sizeof(kCalloutsSuffix)/sizeof(kCalloutsSuffix[0])-1;
    if(n<5 || _wcsicmp(sgo+n-4,L".SGO")!=0 || n-4+suffix+1>cap)return false;
    std::wmemcpy(out,sgo,n-4);
    std::wmemcpy(out+n-4,kCalloutsSuffix,suffix+1);
    return true;
}

// Where model point `p` lands on screen (design units) under the view's model matrix `m`; false: behind the camera.
inline bool Project(const roompreview::Matrix4& m,const float p[3],float aspect,float out[2]) noexcept {
    float z=m.m[3][2];
    for(int i=0;i<3;++i)z+=p[i]*m.m[i][2];
    if(!(z<-0.01f))return false;
    roompreview::ScreenOf(m,p,aspect,out);
    return std::isfinite(out[0]) && std::isfinite(out[1]);
}

// Stacks `n` boxes of height `h` between `top` and `bottom`: each as near its wanted centre `want[i]` as the others
// allow, in the order of their wants, `gap` apart; `y0[i]` its top. More than fit: the top ones keep their place, the
// last ones run past `bottom` (the caller sizes its boxes so a full set fits).
inline void Stack(const float* want,int n,float h,float gap,float top,float bottom,float* y0) noexcept {
    constexpr int kMost=16;
    int order[kMost];
    if(n>kMost)n=kMost;
    for(int i=0;i<n;++i)order[i]=i;
    for(int i=1;i<n;++i)   // by want, ties by index (stable)
        for(int j=i;j>0 && want[order[j]]<want[order[j-1]];--j){const int t=order[j];order[j]=order[j-1];order[j-1]=t;}
    for(int i=0;i<n;++i)y0[i]=want[i]-h*0.5f;
    float floor=top;
    for(int k=0;k<n;++k){float& y=y0[order[k]];if(y<floor)y=floor;floor=y+h+gap;}
    float ceil=bottom-h;
    for(int k=n-1;k>=0;--k){float& y=y0[order[k]];if(y>ceil)y=ceil;ceil=y-h-gap;}
    floor=top;
    for(int k=0;k<n;++k){float& y=y0[order[k]];if(y<floor)y=floor;floor=y+h+gap;}
}

}  // namespace crew::menucallouts

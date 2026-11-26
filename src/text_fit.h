#pragma once
// A line of text fitted into a width (src/ui_text.cpp Labels::DrawFit, every 整备 text with a width: src/armory_draw.cpp).
// Pure: the caller measures (GDI) and passes widths in design units; tests/text_fit_test.cpp.
//
// The order: the line as it is when it fits; else its size scaled down so it does (widths scale with the size) as far
// as MinSizeOf; else, at that size, as many characters as fit with an ellipsis after them. A line that may take two
// rows (the hint bar) is broken at the space run (else the character) that best balances them before it is cut.
#include <cstddef>

namespace crew::textfit {

inline constexpr float kMinScale=0.72f;   // a line is shrunk to this fraction of its size at most
inline constexpr float kMinSize=14.f;     // and never below this (design units; a line already smaller stays as it is)
inline constexpr wchar_t kEllipsis=L'\x2026';

// The smallest size a line of `size` may be drawn at (`minSize` > 0: the caller's own floor).
constexpr float MinSizeOf(float size,float minSize=0.f) noexcept {
    const float m=minSize>0.f ? minSize : (size*kMinScale<kMinSize ? kMinSize : size*kMinScale);
    return m<size ? m : size;
}

// How a line of `natural` width at `size` goes into `maxW` (0: any width): its size, and whether it must be cut.
struct Shrunk { float size=0.f; bool cut=false; };
constexpr Shrunk Shrink(float natural,float size,float maxW,float minSize=0.f) noexcept {
    if(!(maxW>0.f) || !(natural>maxW))return Shrunk{size,false};
    const float floor=MinSizeOf(size,minSize);
    const float s=size*maxW/natural;
    return s>=floor ? Shrunk{s,false} : Shrunk{floor,true};
}

inline bool Space(wchar_t c) noexcept { return c==L' ' || c==L'\x3000' || c==L'\t'; }
inline bool HighSurrogate(wchar_t c) noexcept { return c>=0xD800 && c<=0xDBFF; }

// The characters of `text` [0, n) to keep before the ellipsis so the kept part and `ellipsisW` fit in `maxW`.
// `ends[i]`: the width of text[0..i] (GDI's partial extents). Never splits a surrogate pair; the spaces before the
// cut dropped. 0: not even one character (the ellipsis alone).
inline int Keep(const wchar_t* text,const float* ends,int n,float maxW,float ellipsisW) noexcept {
    const float room=maxW-ellipsisW;
    int k=0;
    while(k<n && ends[k]<=room)++k;
    if(k>0 && k<n && HighSurrogate(text[k-1]))--k;
    while(k>0 && Space(text[k-1]))--k;
    return k;
}

// Where a line of `n` characters is broken into two rows: the first row is [0, end), the second [start, n) (the
// spaces between dropped). `ends` as for Keep. A run of two spaces or more is preferred (the hint bar's "key: action"
// groups are separated by three), then a single space, then any character boundary; among the candidates of the first
// kind there is, the one whose wider row is narrowest. No candidate (n < 2): false.
struct Break { int end=0,start=0; };
inline float Width(const float* ends,int from,int to) noexcept {   // text[from, to)
    if(to<=from)return 0.f;
    return ends[to-1]-(from>0 ? ends[from-1] : 0.f);
}
inline bool BreakTwo(const wchar_t* text,const float* ends,int n,Break* out) noexcept {
    if(!text || !ends || n<2)return false;
    bool found=false;
    float best=0.f;
    for(int pass=0;pass<3 && !found;++pass)   // space runs, single spaces, any character boundary
        for(int i=1;i<n;++i) {
            int end=i,start=i;
            if(pass<2) {
                if(!Space(text[i]) || Space(text[i-1]))continue;   // the first space of a run
                while(start<n && Space(text[start]))++start;
                if(start>=n || (pass==0)!=(start-end>=2))continue;
            } else if(HighSurrogate(text[i-1]))continue;
            const float a=Width(ends,0,end),b=Width(ends,start,n),wider=a>b ? a : b;
            if(!found || wider<best){found=true;best=wider;out->end=end;out->start=start;}
        }
    return found;
}

// --- The whole fit: one row, or two (`size2` > 0: the rows' size), each maybe cut ---
inline constexpr int kMostChars=256;   // longer lines are not fitted (drawn cut at the width)
struct Request { float size=0.f,maxW=0.f,minSize=0.f,size2=0.f; };
struct Row { int from=0,len=0; bool ellipsis=false; };
struct Laid { int rows=1; float size=0.f; Row row[2]{}; };
// `measure(s, n, size, ends)`: the partial extents of s[0, n) at `size` into ends[0, n) (design units); false: it
// could not. False: nothing laid (no text, too long, the measure failed).
template<class Measure> bool Lay(const wchar_t* text,int n,const Request& r,Measure&& measure,Laid* out) noexcept {
    if(!text || n<=0 || n>kMostChars || !out || !(r.size>0.f))return false;
    float ends[kMostChars];
    *out=Laid{};
    out->size=r.size;out->row[0]=Row{0,n,false};
    if(!(r.maxW>0.f))return true;
    if(!measure(text,n,r.size,ends))return false;
    const Shrunk one=Shrink(ends[n-1],r.size,r.maxW,r.minSize);
    out->size=one.size;
    if(!one.cut)return true;
    // a row of [from, from+len) cut to the width at `size` (ends measured at it)
    const auto cut=[&](Row& row,float size) noexcept -> bool {
        float rel[kMostChars],dots=0.f;
        const float base=row.from>0 ? ends[row.from-1] : 0.f;
        for(int i=0;i<row.len;++i)rel[i]=ends[row.from+i]-base;
        if(rel[row.len-1]<=r.maxW)return true;
        if(!measure(&kEllipsis,1,size,&dots))return false;
        row.len=Keep(text+row.from,rel,row.len,r.maxW,dots);
        row.ellipsis=true;
        return true;
    };
    if(r.size2>0.f && measure(text,n,r.size2,ends)) {
        Break b;
        if(BreakTwo(text,ends,n,&b)) {
            const float a=Width(ends,0,b.end),c=Width(ends,b.start,n);
            const Shrunk two=Shrink(a>c ? a : c,r.size2,r.maxW,r.minSize>0.f ? r.minSize*r.size2/r.size : 0.f);
            out->rows=2;out->size=two.size;
            out->row[0]=Row{0,b.end,false};out->row[1]=Row{b.start,n-b.start,false};
            if(!two.cut)return true;
            if(!measure(text,n,two.size,ends))return false;
            return cut(out->row[0],two.size) && cut(out->row[1],two.size);
        }
    }
    out->rows=1;out->row[0]=Row{0,n,false};out->size=one.size;
    if(!measure(text,n,one.size,ends))return false;
    return cut(out->row[0],one.size);
}

}  // namespace crew::textfit

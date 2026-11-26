// A line fitted into a width (src/text_fit.h), offline with a fake font (an ASCII character half the size wide, any
// other a whole size): a line that fits as it is, shrunk as far as the floor, cut with an ellipsis past it (never
// splitting a surrogate pair, no space before the dots), two rows broken at the space run that balances them best
// (else any character), and every laid row inside the width.
#include "../src/text_fit.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {
namespace tf=crew::textfit;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
bool Near(float a,float b) { return std::fabs(a-b)<0.01f; }

int measures=0;
float CharW(wchar_t c,float size) { return (c<0x80 ? 0.5f : 1.f)*size; }
bool Measure(const wchar_t* s,int n,float size,float* ends) noexcept {
    ++measures;
    float w=0;
    for(int i=0;i<n;++i){w+=CharW(s[i],size);ends[i]=w;}
    return true;
}
float WidthOf(const wchar_t* s,int from,int len,bool dots,float size) {
    float w=0;
    for(int i=0;i<len;++i)w+=CharW(s[from+i],size);
    return w+(dots ? CharW(tf::kEllipsis,size) : 0.f);
}
tf::Laid LayOf(const wchar_t* s,float size,float maxW,float size2=0) {
    tf::Laid l;
    Check(tf::Lay(s,static_cast<int>(std::wcslen(s)),tf::Request{size,maxW,0,size2},&Measure,&l),"laid");
    return l;
}
// every row of `l` inside maxW
void Inside(const wchar_t* s,const tf::Laid& l,float maxW,const char* why) {
    for(int k=0;k<l.rows;++k)Check(WidthOf(s,l.row[k].from,l.row[k].len,l.row[k].ellipsis,l.size)<=maxW+0.01f,why);
}

void Floors() {
    Check(Near(tf::MinSizeOf(26),26*tf::kMinScale),"26: shrunk to kMinScale");
    Check(Near(tf::MinSizeOf(18),tf::kMinSize),"18: the absolute floor");
    Check(Near(tf::MinSizeOf(12),12),"12: already below the floor, kept");
    Check(Near(tf::MinSizeOf(26,20),20),"the caller's floor");
    Check(Near(tf::MinSizeOf(26,30),26),"a floor above the size: the size");
    const tf::Shrunk a=tf::Shrink(100,24,0);
    Check(!a.cut && Near(a.size,24),"no width: as it is");
    const tf::Shrunk b=tf::Shrink(100,24,120);
    Check(!b.cut && Near(b.size,24),"fits: as it is");
    const tf::Shrunk c=tf::Shrink(120,24,100);
    Check(!c.cut && Near(c.size,20),"a little wide: scaled to fit");
    const tf::Shrunk d=tf::Shrink(300,24,100);
    Check(d.cut && Near(d.size,tf::MinSizeOf(24)),"far too wide: the floor, cut");
}

void Cuts() {
    // the feedback's line: 「EF31海神直升机 天使级」 26 tall in a 230-wide value
    const wchar_t* heli=L"EF31海神直升机 天使级";
    tf::Laid l=LayOf(heli,26,230);
    Check(l.rows==1,"one row");
    Check(l.size<26 && l.size>=tf::MinSizeOf(26),"shrunk, not below the floor");
    Inside(heli,l,230,"the helicopter inside its width");
    // a narrower one: cut, the dots at the end, the start of the name kept
    l=LayOf(heli,26,150);
    Check(l.row[0].ellipsis && l.row[0].from==0 && l.row[0].len>0,"cut with an ellipsis");
    Check(Near(l.size,tf::MinSizeOf(26)),"cut at the floor size");
    Inside(heli,l,150,"the cut name inside its width");
    // no space before the dots
    const wchar_t* spaced=L"ABCDEFGH    IJKLMNOPQRSTUVWXYZ";
    const float fs=tf::MinSizeOf(20);
    l=LayOf(spaced,20,8*0.5f*fs+CharW(tf::kEllipsis,fs)+2*0.5f*fs);
    Check(l.row[0].ellipsis && l.row[0].len==8,"the spaces before the cut dropped");
    // a surrogate pair is not split
    const wchar_t pair[]={L'A',0xD83D,0xDE00,L'B',L'C',L'D',L'E',L'F',L'G',L'H',L'I',0};
    float ends[16];
    Measure(pair,11,10,ends);
    Check(tf::Keep(pair,ends,11,ends[1]+0.1f,0)==1,"the high half alone dropped");
    Check(tf::Keep(pair,ends,11,ends[2]+0.1f,0)==3,"the pair kept whole");
    // too narrow for one character: the dots alone
    l=LayOf(L"漢字漢字",24,tf::MinSizeOf(24)*2.2f);   // a character and the dots, a size wide each
    Check(l.row[0].ellipsis && l.row[0].len==1,"one character and the dots");
    l=LayOf(L"漢字漢字",24,tf::MinSizeOf(24)*1.5f);
    Check(l.row[0].ellipsis && l.row[0].len==0,"the dots alone");
    // a line that fits measures once and is kept as it is
    measures=0;
    l=LayOf(L"short",24,500);
    Check(measures==1 && l.rows==1 && !l.row[0].ellipsis && l.row[0].len==5 && Near(l.size,24),"fits: as it is");
}

void Rows() {
    // the hint bar's keys: groups three spaces apart, broken between groups
    const wchar_t* keys=L"Enter/A：选武器   Z/C 或 LT/RT：兵种   Q/E 或 LB/RB：切页";
    const int n=static_cast<int>(std::wcslen(keys));
    float ends[tf::kMostChars];
    Measure(keys,n,19,ends);
    tf::Break b;
    Check(tf::BreakTwo(keys,ends,n,&b),"a break");
    Check(keys[b.end]==L' ' && keys[b.start-1]==L' ' && keys[b.start]!=L' ',"between groups, the spaces dropped");
    Check(b.start-b.end==3,"at a group's three spaces, not a single one inside a group");
    // the balanced one: its wider row no wider than at any other three-space run
    const float wider=std::fmax(tf::Width(ends,0,b.end),tf::Width(ends,b.start,n));
    for(int i=1;i+3<n;++i)
        if(keys[i]==L' ' && keys[i+1]==L' ' && keys[i+2]==L' ' && keys[i-1]!=L' ')
            Check(wider<=std::fmax(tf::Width(ends,0,i),tf::Width(ends,i+3,n))+0.01f,"the most balanced group break");
    // single spaces only: one of them
    const wchar_t* single=L"Z/C 或 LT/RT";
    Measure(single,11,19,ends);
    Check(tf::BreakTwo(single,ends,11,&b) && single[b.end]==L' ' && b.start==b.end+1,"a single space");
    // no spaces: any character
    const wchar_t* cjk=L"输入名称候选完成取消";
    Measure(cjk,10,19,ends);
    Check(tf::BreakTwo(cjk,ends,10,&b) && b.end==5 && b.start==5,"no space: the middle character");
    Check(!tf::BreakTwo(L"A",ends,1,&b),"one character: no break");
    // laid in two rows when one does not fit even shrunk, each row inside the width
    const float wide=WidthOf(keys,0,n,false,24);
    tf::Laid l=LayOf(keys,24,wide*0.6f,19);
    Check(l.rows==2,"two rows");
    Check(!l.row[0].ellipsis && !l.row[1].ellipsis,"whole on two rows");
    Check(l.row[0].from==0 && l.row[1].from+l.row[1].len==n,"the rows cover the line");
    Inside(keys,l,wide*0.6f,"both rows inside");
    // fits on one row shrunk: one row (two only when one cannot)
    l=LayOf(keys,24,wide*0.9f,19);
    Check(l.rows==1 && !l.row[0].ellipsis,"one row, shrunk");
    // far too narrow even for two rows: both cut, inside
    l=LayOf(keys,24,wide*0.2f,19);
    Check(l.rows==2 && (l.row[0].ellipsis || l.row[1].ellipsis),"two rows, cut");
    Inside(keys,l,wide*0.2f,"cut rows inside");
    // without size2: one row cut
    l=LayOf(keys,24,wide*0.6f);
    Check(l.rows==1 && l.row[0].ellipsis,"no second row asked: cut");
}

void Refusals() {
    tf::Laid l;
    Check(!tf::Lay(L"",0,tf::Request{24,100,0,0},&Measure,&l),"empty: not laid");
    wchar_t longer[tf::kMostChars+2];
    for(int i=0;i<tf::kMostChars+1;++i)longer[i]=L'x';
    longer[tf::kMostChars+1]=0;
    Check(!tf::Lay(longer,tf::kMostChars+1,tf::Request{24,100,0,0},&Measure,&l),"too long: not laid");
    const auto broken=[](const wchar_t*,int,float,float*) noexcept { return false; };
    Check(!tf::Lay(L"abc",3,tf::Request{24,10,0,0},broken,&l),"the measure failed: not laid");
}
}  // namespace

int main() {
    Floors();
    Cuts();
    Rows();
    Refusals();
    std::printf("text_fit: %d checks passed\n",checks);
    return 0;
}

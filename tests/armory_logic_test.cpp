// 整备 screen state machine (src/armory_logic.h), offline: keys fire on the frame they go down (the opening key held
// does nothing), tabs / sub tabs / columns / preset chips by keys and mouse, page edits committed before a tab change,
// a preset operation and close (a failed commit keeps the screen), the preset popup's flows (store, new / copy /
// rename with typed or suggested names, delete asking again), and a long random walk keeping every index in range,
// every effect well formed and every View inside its capacity.
#include "../src/armory_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <random>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew::armory;

// A page whose counts depend on the cursor (rows -> items -> fields), counting edits and commits.
class FakePage final : public Page {
public:
    const wchar_t* name=L"P";
    int subs=0;
    int rows=5;
    int steps=0,acts=0,commits=0;
    bool failCommit=false;
    bool dirty=false;
    float top1=0;               // ColumnTop(1)
    skin::Rect hole{};          // Hole (W 0: none)
    skin::Rect owned{};         // Click takes clicks inside it
    int cycles=0,ownClicks=0,discards=0;
    const wchar_t* Name() const noexcept override { return name; }
    int Subs() const noexcept override { return subs; }
    const wchar_t* SubName(int) const noexcept override { return L"S"; }
    int Rows(int sub) const noexcept override { return rows+sub; }
    void RowLine(int,int row,Line* out) const noexcept override {
        std::swprintf(out->label,kTextMost,L"row %d",row);
    }
    int Items(const Pos& p) const noexcept override { return p.row%3==2 ? 0 : 3+p.row; }
    void ItemLine(const Pos& p,Line* out) const noexcept override {
        std::swprintf(out->label,kTextMost,L"item %d",p.item);
        std::swprintf(out->value,kTextMost,L"%d",p.item*2);
    }
    int Fields(const Pos& p) const noexcept override { return p.item%2 ? 0 : 2+p.item; }
    void FieldLine(const Pos& p,Line* out) const noexcept override {
        std::swprintf(out->label,kTextMost,L"field %d",p.field);
        std::swprintf(out->value,kTextMost,L"v%d",steps);
        out->steps=true;
    }
    bool Step(const Pos&,int d) noexcept override { steps+=d; dirty=true; return true; }
    bool Activate(const Pos&) noexcept override { ++acts; dirty=true; return true; }
    bool Commit(wchar_t* why,std::size_t cap) noexcept override {
        if(failCommit){CopyText(why,cap,L"bad");return false;}
        ++commits; dirty=false; return true;
    }
    bool Dirty() const noexcept override { return dirty; }
    // the page hooks (off unless a test turns them on)
    int header[3]{};
    bool holed=false, typing=false;
    int alts=0, relocateItem=-1;
    wchar_t typed[kTextMost]{};
    const wchar_t* note=nullptr;
    int Header(int c) const noexcept override { return header[c]; }
    // `holed`: the weapons page's details hole; else `hole` (the support pages' viewport)
    bool Hole(const Pos&,skin::Rect* out) const noexcept override {
        if(holed){*out=skin::Rect{1328,130,1792,530};return true;}
        *out=hole;
        return hole.W()>0;
    }
    bool Key(Pos&,int,int k,const Input& in) noexcept override {
        if(typing){
            if(k==kConfirm || k==kCancel){typing=false;return true;}
            if(k==kBackspace){detail::PopChar(typed);return true;}
            return false;
        }
        if(k==kSearch){typing=true;return true;}
        if(k==kAltNext){++alts;return true;}
        if(k==kMouseLeft && header[1]>0 && in.pointer && skin::layout::Row(1,0).Contains(in.x,in.y)){++alts;return true;}
        return false;
    }
    bool Typing() const noexcept override { return typing; }
    void Type(const wchar_t* c,int n) noexcept override { for(int i=0;i<n;++i)detail::AppendChar(typed,c[i]); }
    void Relocate(Pos* p,int) noexcept override { if(relocateItem>=0){p->item=relocateItem;relocateItem=-1;} }
    bool TakeNote(wchar_t* out,std::size_t cap) noexcept override {
        if(!note)return false;
        CopyText(out,cap,note);
        note=nullptr;
        return true;
    }
    void Discard() noexcept override { ++discards; dirty=false; }
    float ColumnTop(int column) const noexcept override { return column==1 ? top1 : 0; }
    bool Click(Pos&,float x,float y) noexcept override {
        if(!owned.Contains(x,y))return false;
        ++ownClicks;
        return true;
    }
    bool Cycle(const Pos&,int d) noexcept override { cycles+=d; return true; }
};

Words MakeWords() {
    Words w;
    w.title=L"整备";
    w.actions[0]=L"新建";w.actions[1]=L"复制";w.actions[2]=L"改名";w.actions[3]=L"删除";w.actions[4]=L"存入";
    w.confirmDelete=L"再按确认删除";
    w.renameTitle=L"名称";
    for(auto& h:w.hints)h=L"hint";
    w.suggest[0]=L"空战";w.suggest[1]=L"地面";w.nsuggest=2;
    return w;
}
Presets MakePresets(int count,int current) {
    Presets p;
    p.count=count;p.current=current;
    for(int i=0;i<count;++i)std::swprintf(p.names[i],kTextMost,L"方案%d",i+1);
    return p;
}

struct Rig {
    FakePage a,b,c;
    Page* pages[3]{&a,&b,&c};
    State s;
    Presets ps=MakePresets(3,0);
    Words w=MakeWords();
    Input in;
    Rig() { a.name=L"A";b.name=L"B";c.name=L"C";b.subs=2; Open(s,in); }
    // Press `k` for one frame then release it (two Steps); the effects of the press.
    Effects Tap(int k) {
        in.held[k]=true;
        const Effects fx=Step(s,pages,3,ps,w,in);
        in.held[k]=false;
        Step(s,pages,3,ps,w,in);
        return fx;
    }
    Effects Click(float x,float y,bool right=false) {
        in.pointer=true;in.x=x;in.y=y;
        return Tap(right ? kMouseRight : kMouseLeft);
    }
    Effects Type(const wchar_t* t) {
        in.ntyped=0;
        for(;*t && in.ntyped<8;++t)in.typed[in.ntyped++]=*t;
        const Effects fx=Step(s,pages,3,ps,w,in);
        in.ntyped=0;
        return fx;
    }
};

void Scripted() {
    // the key that opened the screen (still held) does not close it; pressed again it does
    {
        Rig r;
        r.in.held[kToggle]=true;
        Open(r.s,r.in);
        Effects fx=Step(r.s,r.pages,3,r.ps,r.w,r.in);
        Check(!fx.close && r.s.open,"held opening key ignored");
        r.in.held[kToggle]=false;
        Step(r.s,r.pages,3,r.ps,r.w,r.in);
        fx=r.Tap(kToggle);
        Check(fx.close && !r.s.open && r.a.commits==1,"toggle closes after commit");
    }
    // tabs: Q/E wrap and commit; a failed commit keeps the tab and notes why
    {
        Rig r;
        r.Tap(kTabNext);
        Check(r.s.tab==1 && r.a.commits==1,"tab next commits the page");
        r.Tap(kTabPrev);r.Tap(kTabPrev);
        Check(r.s.tab==2,"tabs wrap");
        r.c.failCommit=true;
        r.Tap(kTabNext);
        Check(r.s.tab==2 && r.s.note[0]==L'b',"failed commit keeps the tab, says why");
        Effects fx=r.Tap(kDigit1+1);
        Check(fx.op==Op::none,"failed commit blocks a preset switch");
        r.c.failCommit=false;
        fx=r.Tap(kDigit1+1);
        Check(fx.op==Op::switchTo && fx.index==1,"digit 2 switches to preset 2");
        fx=r.Tap(kDigit1+5);
        Check(fx.op==Op::none,"digit past the presets does nothing");
    }
    // columns: right/confirm in, left/cancel out, details step and activate, cancel in the list closes
    {
        Rig r;
        r.Tap(kDown);
        Check(r.s.pos.row==1,"down moves the list");
        r.Tap(kUp);r.Tap(kUp);
        Check(r.s.pos.row==4,"list wraps");
        r.Tap(kDown);
        Check(r.s.pos.row==0,"list wraps back");
        r.Tap(kConfirm);
        Check(r.s.column==1,"confirm into the centre");
        r.Tap(kRight);
        Check(r.s.column==2,"right into the details");
        r.Tap(kRight);r.Tap(kRight);r.Tap(kLeft);
        Check(r.a.steps==1,"left / right step the field");
        r.Tap(kConfirm);
        Check(r.a.acts==1,"confirm activates the field");
        r.Tap(kCancel);
        Check(r.s.column==1,"cancel back to the centre");
        r.Tap(kDown);
        Check(r.s.pos.item==1,"centre moves");
        r.Tap(kRight);
        Check(r.s.column==1,"an item without fields keeps the centre");
        r.Tap(kLeft);
        Check(r.s.column==0,"left back to the list");
        r.Tap(kDown);r.Tap(kDown);
        Check(r.s.pos.row==2,"row 2 has no items");
        r.Tap(kConfirm);
        Check(r.s.column==0,"no items: stays in the list");
        Effects fx=r.Tap(kCancel);
        Check(!fx.close && r.s.open && r.s.popup==Popup::unsaved && r.s.popupCursor==kUnsavedSave,
              "cancel in the list with edits asks first");
        fx=r.Tap(kConfirm);
        Check(fx.close && !r.s.open && r.a.commits==1 && !r.a.dirty,"save and close commits");
    }
    // the close prompt: discard, back, toggle; a clean page closes at once
    {
        Rig r;
        r.a.dirty=true;
        r.Tap(kToggle);
        Check(r.s.open && r.s.popup==Popup::unsaved,"toggle with edits asks first");
        r.Tap(kCancel);
        Check(r.s.open && r.s.popup==Popup::none && r.a.dirty,"cancel the prompt: back, edits kept");
        r.Tap(kToggle);
        r.Tap(kToggle);
        Check(r.s.open && r.s.popup==Popup::none,"toggle on the prompt: back");
        r.Tap(kCancel);
        r.Tap(kDown);
        Check(r.s.popupCursor==kUnsavedDiscard,"prompt cursor");
        Effects fx=r.Tap(kConfirm);
        Check(fx.close && !r.s.open && r.a.discards==1 && r.a.commits==0,"discard and close");
        Rig q;
        q.a.dirty=true;
        q.Tap(kCancel);
        const skin::Rect line=detail::UnsavedLine(kUnsavedBack);
        q.Click((line.x0+line.x1)/2,(line.y0+line.y1)/2);
        Check(q.s.open && q.s.popup==Popup::none,"click back");
        q.a.failCommit=true;
        q.Tap(kCancel);
        fx=q.Tap(kConfirm);
        Check(!fx.close && q.s.open && q.s.note[0]==L'b',"a failed save keeps the screen, says why");
    }
    // page extras: an action item, a reserved column top, an owned click, Z / C without sub tabs, the hole
    {
        Rig r;
        r.Tap(kConfirm);
        r.Tap(kDown);
        Check(r.s.column==1 && r.s.pos.item==1,"on an item without fields");
        r.Tap(kConfirm);
        Check(r.a.acts==1 && r.s.column==1,"confirm on an item without fields activates it");
        const skin::Rect plain=detail::LineRect(r.a,1,1);
        r.a.top1=400;
        const skin::Rect lowered=detail::LineRect(r.a,1,1);
        Check(lowered.y0>=plain.y0+400 && detail::Visible(r.a,1)==detail::Visible(r.a,0)-7,"column top takes whole rows");
        r.Click((lowered.x0+lowered.x1)/2,(lowered.y0+lowered.y1)/2);
        Check(r.a.acts==2,"second click on an action item activates it");
        r.a.owned=skin::Rect{700,150,800,200};
        r.Click(750,170);
        Check(r.a.ownClicks==1 && r.a.acts==2 && r.s.column==1,"the page's own click comes first");
        r.Tap(kSubNext);r.Tap(kSubNext);r.Tap(kSubPrev);
        Check(r.a.cycles==1,"Z / C cycle a page without sub tabs");
        r.a.hole=skin::Rect{640,130,1270,520};
        static View v;
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(v.layer[0].holeQuads>300 && v.layer[0].holeQuads<v.layer[0].quads.n && v.layer[0].hole.x0==640,
              "hole: the background before the rows");
        r.a.hole=skin::Rect{};
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(v.layer[0].holeQuads==0 && v.layer[0].hole.W()==0,"no hole");
        // the status strip: the lines under the hint bar, side by side, a warning in its colour, at most kStatusMost
        StatusLines st;
        for(int i=0;i<kStatusMost+2;++i)st.Add(i==1 ? L"warn" : L"line",i==1);
        st.Add(L"",false);
        Check(st.n==kStatusMost,"status lines capped");
        Build(r.s,r.pages,3,r.ps,r.w,&v,&st);
        int under=0,warned=0;
        float lastX=-1;
        bool ordered=true;
        for(int i=0;i<v.layer[0].nt;++i){
            const Text& t=v.layer[0].texts[i];
            if(t.y<skin::layout::kHint.y1)continue;
            ++under;
            if(t.argb==kStatusWarn)++warned;
            ordered=ordered && t.x>lastX && t.maxW>0;
            lastX=t.x;
        }
        Check(under==kStatusMost && warned==1 && ordered,"status strip under the hint bar");
    }
    // sub tabs
    {
        Rig r;
        r.Tap(kTabNext);
        r.Tap(kSubNext);
        Check(r.s.pos.sub==1 && r.s.column==0,"sub tab next");
        r.Tap(kSubNext);
        Check(r.s.pos.sub==0,"sub tabs wrap");
        const crew::armory::skin::Rect st=detail::SubTab(r.b,1);
        r.Click(st.x0+5,st.y0+5);
        Check(r.s.pos.sub==1,"click a sub tab");
    }
    // mouse: tabs, chips, rows, fields
    {
        Rig r;
        namespace L=crew::armory::skin::layout;
        r.Click(L::Tab(2).x0+10,L::Tab(2).y0+10);
        Check(r.s.tab==2,"click a tab");
        Effects fx=r.Click(L::Chip(1,4).x0+5,L::Chip(1,4).y0+5);
        Check(fx.op==Op::switchTo && fx.index==1,"click a chip switches");
        fx=r.Click(L::Chip(3,4).x0+5,L::Chip(3,4).y0+5);
        Check(fx.op==Op::none && r.s.popup==Popup::rename && r.s.renameAction==kActNew,"click ＋ asks a name");
        r.Tap(kCancel);
        Check(r.s.popup==Popup::none,"cancel the name");
        r.Click(L::Row(0,3).x0+5,L::Row(0,3).y0+5);
        Check(r.s.pos.row==3 && r.s.column==0,"click a row");
        r.Click(L::Row(1,1).x0+5,L::Row(1,1).y0+5);
        Check(r.s.pos.item==1 && r.s.column==1,"click an item");
        r.Click(L::Row(1,2).x0+5,L::Row(1,2).y0+5);
        r.Click(L::Row(2,1).x0+5,L::Row(2,1).y0+5);
        Check(r.s.pos.field==1 && r.s.column==2 && r.c.acts==0,"click a field focuses it");
        r.Click(L::Row(2,1).x0+5,L::Row(2,1).y0+5);
        Check(r.c.acts==1,"second click activates");
        r.in.wheel=1;
        Step(r.s,r.pages,3,r.ps,r.w,r.in);
        r.in.wheel=0;
        Check(r.c.steps==1,"wheel steps a details field");
        fx=r.Click(L::Chip(0,4).x0+5,L::Chip(0,4).y0+5,true);
        Check(fx.op==Op::none && r.s.popup==Popup::actions && r.s.popupTarget==0,"right click a chip: its popup");
    }
    // popup: store, delete asks again, copy with a typed name, rename with a suggestion, new
    {
        Rig r;
        r.ps=MakePresets(3,1);
        Effects fx=r.Tap(kStore);
        Check(fx.op==Op::store && fx.index==1,"R stores into the current preset");
        r.Tap(kPresetMenu);
        Check(r.s.popup==Popup::actions && r.s.popupCursor==kActStore && r.s.popupTarget==1,"F opens the popup");
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::store && r.s.popup==Popup::none,"popup store");
        r.Tap(kPresetMenu);
        r.Tap(kUp);
        Check(r.s.popupCursor==kActDelete,"popup cursor up");
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::none && r.s.popup==Popup::confirmDelete,"delete asks again");
        r.Tap(kCancel);
        Check(r.s.popup==Popup::actions,"cancel back to the actions");
        r.Tap(kConfirm);
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::remove && fx.index==1,"delete confirmed");
        r.Tap(kPresetMenu);
        r.s.popupCursor=kActCopy;
        r.Tap(kConfirm);
        Check(r.s.popup==Popup::rename && std::wcscmp(r.s.renameText,L"方案2")==0,"copy starts from the name");
        r.Type(L"x");
        r.Tap(kBackspace);r.Tap(kBackspace);
        r.Type(L"B");
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::copy && fx.index==1 && std::wcscmp(fx.name,L"方案B")==0,"copy with a typed name");
        r.Tap(kPresetMenu);
        r.s.popupCursor=kActRename;
        r.Tap(kConfirm);
        r.Tap(kDown);
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::rename && std::wcscmp(fx.name,L"空战")==0,"pad picks a suggested name");
        r.Tap(kPresetMenu);
        r.s.popupCursor=kActNew;
        r.Tap(kConfirm);
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::none && r.s.popup==Popup::rename,"an empty name is not taken");
        r.Type(L"N\x01");
        fx=r.Tap(kConfirm);
        Check(fx.op==Op::add && std::wcscmp(fx.name,L"N")==0,"new preset, control characters dropped");
        // full book: no new / copy
        r.ps=MakePresets(kPresetMost,0);
        r.Tap(kPresetMenu);
        r.s.popupCursor=kActNew;
        r.Tap(kConfirm);
        Check(r.s.popup==Popup::actions,"no new preset past the most");
        r.Tap(kCancel);
        Check(r.s.popup==Popup::none,"cancel closes the popup");
    }
    // the view: open draws two layers within capacity, closed draws nothing
    {
        Rig r;
        static View v;
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(v.open && v.layer[0].quads.n>300 && v.layer[1].quads.n==0,"view: screen, no popup");
        Check(v.layer[0].quads.dropped==0 && v.layer[0].droppedTexts==0,"view within capacity");
        r.Tap(kPresetMenu);
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(v.layer[1].quads.n>0 && v.layer[1].nt==1+kActCount,"popup layer");
        r.s.popup=Popup::none;
        r.a.dirty=true;
        r.Tap(kToggle);
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(r.s.popup==Popup::unsaved && v.layer[1].nt==1+kUnsavedCount,"close prompt layer");
        Close(r.s);
        Build(r.s,r.pages,3,r.ps,r.w,&v);
        Check(!v.open && v.layer[0].quads.n==0,"closed: nothing");
    }
}

// The page hooks: header lines, the hole, keys a page takes, typing, relocation, notes.
void Hooks() {
    Rig r;
    r.a.header[1]=2;r.a.header[2]=7;r.a.holed=true;r.a.rows=6;
    r.Tap(kConfirm);
    Check(r.s.column==1,"into the centre");
    Check(detail::Visible(r.a,1)==skin::layout::RowsIn(1)-2,"header lines leave fewer visible");
    // a click on the first item line (below the header) focuses it; a click on the header goes to the page
    const skin::Rect item1=skin::layout::Row(1,2+1);
    r.Click((item1.x0+item1.x1)/2,(item1.y0+item1.y1)/2);
    Check(r.s.pos.item==1 && r.s.column==1,"items start below the header");
    const skin::Rect head=skin::layout::Row(1,0);
    r.Click((head.x0+head.x1)/2,(head.y0+head.y1)/2);
    Check(r.a.alts==1 && r.s.pos.item==1,"the page takes a click on its header");
    r.Tap(kAltNext);
    Check(r.a.alts==2,"the page takes its own key");
    // typing: characters to the page, the state's keys (tab, preset, list) do nothing
    r.Tap(kSearch);
    Check(r.a.typing,"search starts typing");
    r.Type(L"qe1");
    r.Tap(kTabNext);r.Tap(kDigit1+1);r.Tap(kDown);
    Check(std::wcscmp(r.a.typed,L"qe1")==0,"typed characters reach the page");
    Check(r.s.tab==0 && r.s.pos.item==1,"while typing the state's keys do nothing");
    r.Tap(kBackspace);
    Check(std::wcscmp(r.a.typed,L"qe")==0,"backspace to the page");
    r.Tap(kConfirm);
    Check(!r.a.typing && r.s.column==1,"confirm ends typing without moving a column");
    r.Type(L"x");
    Check(std::wcscmp(r.a.typed,L"qe")==0,"not typing: characters ignored");
    // relocation and the note
    r.a.relocateItem=2;
    r.Tap(kUp);
    Check(r.s.pos.item==2,"the page relocates the cursor last");
    r.a.note=L"why";
    r.Tap(kUp);
    Check(std::wcscmp(r.s.note,L"why")==0,"the page's note shown");
    // the toggle still closes while typing
    r.Tap(kSearch);
    Effects fx=r.Tap(kToggle);
    Check(fx.close && !r.s.open,"toggle closes while typing");
    // the view: nothing drawn over the hole
    Open(r.s,r.in);
    static View v;
    Build(r.s,r.pages,3,r.ps,r.w,&v);
    const skin::Rect hole{1328,130,1792,530};
    for(int i=0;i<v.layer[0].quads.n;++i){
        const skin::Quad& q=v.layer[0].quads.q[i];
        float x0=q.xy[0][0],x1=x0,y0=q.xy[0][1],y1=y0;
        for(const auto& c:q.xy){x0=c[0]<x0?c[0]:x0;x1=c[0]>x1?c[0]:x1;y0=c[1]<y0?c[1]:y0;y1=c[1]>y1?c[1]:y1;}
        Check(!(x0<hole.x1-0.01f && hole.x0+0.01f<x1 && y0<hole.y1-0.01f && hole.y0+0.01f<y1),"nothing over the hole");
    }
    for(int i=0;i<v.layer[0].nt;++i){
        const Text& t=v.layer[0].texts[i];
        Check(!hole.Contains(t.x,t.y),"no text in the hole");
    }
    Check(v.layer[0].quads.dropped==0,"holed view within capacity");
}

void RandomWalk() {
    std::mt19937 rng(20261124u);
    static View v;
    for(int run=0;run<40;++run){
        Rig r;
        r.a.rows=static_cast<int>(rng()%30);
        r.c.rows=1+static_cast<int>(rng()%3);
        r.c.header[0]=static_cast<int>(rng()%3);r.c.header[1]=static_cast<int>(rng()%3);r.c.header[2]=7;
        r.c.holed=rng()%2!=0;
        int closes=0;
        for(int f=0;f<4000;++f){
            if(!r.s.open){Open(r.s,r.in,static_cast<int>(rng()%3));}
            if(rng()%97==0){r.ps=MakePresets(static_cast<int>(rng()%(kPresetMost+1)),-1);
                r.ps.current=r.ps.count ? static_cast<int>(rng()%r.ps.count) : -1;}
            if(rng()%211==0)r.a.rows=static_cast<int>(rng()%30);
            r.b.failCommit=rng()%13==0;
            if(rng()%101==0)r.a.top1=static_cast<float>(rng()%800);
            if(rng()%103==0)r.a.hole=rng()%2 ? skin::Rect{640,130,1270,520} : skin::Rect{};
            for(int k=0;k<kKeyCount;++k)
                if(rng()%9==0)r.in.held[k]=!r.in.held[k];
            if(r.in.held[kToggle] && rng()%2)r.in.held[kToggle]=false;
            r.in.pointer=rng()%4!=0;
            r.in.x=static_cast<float>(rng()%1920);r.in.y=static_cast<float>(rng()%1080);
            r.in.wheel=static_cast<int>(rng()%7)-3;
            r.in.ntyped=static_cast<int>(rng()%3);
            for(int i=0;i<r.in.ntyped;++i)r.in.typed[i]=static_cast<wchar_t>(rng()%0x80);
            const Effects fx=Step(r.s,r.pages,3,r.ps,r.w,r.in);
            if(fx.close){++closes;Check(!r.s.open,"close effect closes");}
            switch(fx.op){
            case Op::none:break;
            case Op::add:Check(r.ps.count<kPresetMost && fx.name[0],"add: room and a name");break;
            case Op::copy:Check(r.ps.count<kPresetMost && fx.index>=0 && fx.index<r.ps.count && fx.name[0],"copy");break;
            case Op::rename:Check(fx.index>=0 && fx.index<r.ps.count && fx.name[0],"rename");break;
            default:Check(fx.index>=0 && fx.index<r.ps.count,"preset op on an existing preset");break;
            }
            if(!r.s.open)continue;
            Page& p=*r.pages[r.s.tab];
            Check(r.s.tab>=0 && r.s.tab<3 && r.s.column>=0 && r.s.column<3,"tab / column in range");
            for(int c=0;c<3;++c){
                const int n=detail::Count(p,r.s.pos,c), i=detail::IndexOf(r.s.pos,c);
                Check(n>0 ? (i>=0 && i<n) : i==0,"cursor inside its column");
                Check(i>=r.s.top[c] && (n==0 || i<r.s.top[c]+detail::Visible(p,c)),"cursor scrolled into view");
            }
            Check(r.s.column==0 || detail::Count(p,r.s.pos,r.s.column)>0,"focused column has lines");
            Check(std::wcslen(r.s.renameText)<kTextMost,"rename text terminated");
            if(f%50==0){
                Build(r.s,r.pages,3,r.ps,r.w,&v);
                Check(v.layer[0].quads.dropped==0 && v.layer[1].quads.dropped==0,"walk: quads fit");
                Check(v.layer[0].droppedTexts==0 && v.layer[1].droppedTexts==0,"walk: texts fit");
            }
        }
        Check(closes>0,"the walk closes the screen sometimes");
    }
}
}  // namespace

int main() {
    Scripted();
    Hooks();
    RandomWalk();
    std::printf("armory_logic: %d checks passed\n",checks);
    return 0;
}

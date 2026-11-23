#pragma once
// 整备 screen state machine (docs/plan-2026-11-24-ui-rewrite.md stage 2), pure: no game, no Windows, no D3D. The plugin
// (src/armory.cpp) turns keyboard / pad / mouse into an Input each UI frame, calls Step, carries out the Effects it
// returns (preset book operations need the ini and the GameStatus) and publishes Build's View to the render thread
// (src/armory_draw.cpp paints it). tests/armory_logic_test.cpp drives it offline, a random walk included.
//
// Screen: title bar (name, page tabs, preset chips), three columns (list -> centre -> details), hint bar; a popup for
// the preset actions. Keys are edge-triggered: an action fires on the frame a key goes down, so the key that opened
// the screen (still held) does nothing until released and pressed again.
//   page tabs        Q / E, pad LB / RB, a click on a tab
//   sub tabs         Z / C, pad LT / RT (a page with Subs() > 0; drawn on top of the list)
//   presets          1..8 switch, R / pad X store the screen's state into the current preset, F / pad Y the preset
//                    popup (新建 / 复制 / 改名 / 删除, delete asks again), a click on a chip switches (on ＋ adds), a
//                    right click on a chip opens the popup for that preset
//   columns          right / confirm moves list -> centre -> details, left / cancel back; in details left / right
//                    step the field, confirm activates it; cancel in the list closes the screen; the toggle key closes
//   mouse            click a row / item / field focuses it (a second click on the focused field activates it, as
//                    on a focused item that has no fields), the wheel moves the cursor (on a details field: steps it)
//   page extras      Z / C on a page without sub tabs: Page::Cycle (the 3D view's camera); a click the page owns
//                    (Page::Click: its tabs / buttons drawn over its columns) before the columns see it; confirm on a
//                    centre item without fields: Page::Activate (an action item)
// A page's edits are committed (Page::Commit) before the tab changes and before any preset operation; a failed commit
// keeps the screen where it is and shows why. Closing with edits (Page::Dirty) asks first: save and close / discard
// and close / back (Popup::unsaved); the tab of a page with edits carries the unsaved mark.
// A page may reserve the top of a column (Page::ColumnTop: the 3D view over the centre rows, a panel over the details)
// and name a rect of the screen left uncovered (Page::Hole): the game draws the 3D preview before any UI layer, so
// the background quads (Layer::holeQuads) are painted around it (src/armory_draw.cpp).
#include "armory_skin.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace crew::armory {

inline constexpr std::size_t kTextMost=64;
inline constexpr int kPageMost=6, kPresetMost=8, kSuggestMost=8;

inline void CopyText(wchar_t* to,std::size_t cap,const wchar_t* from) noexcept {
    if(cap==0)return;
    std::size_t i=0;
    if(from)
        for(;i+1<cap && from[i];++i)to[i]=from[i];
    to[i]=0;
}

// A line of a column: the label, optionally a value (details: drawn right, with arrows when the field steps).
struct Line {
    wchar_t label[kTextMost]{};
    wchar_t value[kTextMost]{};
    bool steps=false;   // left / right change it (Page::Step)
    bool acts=false;    // confirm does something (Page::Activate)
    bool dim=false;     // shown greyed (unavailable)
    std::uint32_t chip=0;   // a colour swatch (0xAARRGGBB) beside the value; 0 none
};

// Where the cursor is on a page.
struct Pos { int sub=0,row=0,item=0,field=0; };

// ---- what the render thread draws ------------------------------------------------------------------------------------
enum class Align : std::uint8_t { left,centre,right };
struct Text {
    wchar_t s[kTextMost]{};
    float x=0,y=0,size=24,maxW=0;
    std::uint32_t argb=skin::kText;
    Align align=Align::left;
    bool bold=false;
};
struct Layer {
    static constexpr int kTextMost=320;
    skin::Quads quads;
    Text texts[kTextMost];
    int nt=0;
    int droppedTexts=0;
    // The rect left uncovered (W / H 0: none): quads [0, holeQuads) are painted around it, the rest over it.
    skin::Rect hole{};
    int holeQuads=0;
    void Say(const wchar_t* s,float x,float y,float size,std::uint32_t argb,Align a=Align::left,bool bold=false,
             float maxW=0) noexcept {
        if(nt>=kTextMost){++droppedTexts;return;}
        Text& t=texts[nt++];
        CopyText(t.s,kTextMost,s);
        t.x=x;t.y=y;t.size=size;t.argb=argb;t.align=a;t.bold=bold;t.maxW=maxW;
    }
};
// Two layers: the screen, then the popup over it (quads of a layer under its texts).
struct View {
    Layer layer[2];
    bool open=false;
    void Clear() noexcept {
        for(Layer& l:layer){l.quads.n=0;l.quads.dropped=0;l.nt=0;l.droppedTexts=0;l.hole=skin::Rect{};l.holeQuads=0;}
        open=false;
    }
};

// ---- a page ----------------------------------------------------------------------------------------------------------
// One tab of the screen. Called on the UI thread only (Step / Build). Counts may change between frames (the state
// clamps its cursor every frame).
class Page {
public:
    virtual const wchar_t* Name() const noexcept=0;
    virtual int Subs() const noexcept { return 0; }
    virtual const wchar_t* SubName(int) const noexcept { return L""; }
    // Left column.
    virtual int Rows(int sub) const noexcept=0;
    virtual void RowLine(int sub,int row,Line* out) const noexcept=0;
    // Centre column: the items of the row at p.sub / p.row.
    virtual int Items(const Pos& p) const noexcept=0;
    virtual void ItemLine(const Pos& p,Line* out) const noexcept=0;   // p.item
    // Right column: the fields of the item at p.sub / p.row / p.item.
    virtual int Fields(const Pos& p) const noexcept=0;
    virtual void FieldLine(const Pos& p,Line* out) const noexcept=0;  // p.field
    // Edits (the page keeps them until Commit / Discard). True when something changed.
    virtual bool Step(const Pos&,int /*delta*/) noexcept { return false; }
    virtual bool Activate(const Pos&) noexcept { return false; }
    // Write the page's edits; false keeps them and says why.
    virtual bool Commit(wchar_t* /*why*/,std::size_t /*cap*/) noexcept { return true; }
    virtual void Discard() noexcept {}
    virtual bool Dirty() const noexcept { return false; }
    // Extra marks over the columns (a picture, a summary), after the state's own: column 0..2.
    virtual void Draw(const Pos&,int /*column*/,Layer*) const noexcept {}
    // Design units reserved at the top of column 0..2 (its rows start below; Draw fills it). 0: none.
    virtual float ColumnTop(int /*column*/) const noexcept { return 0; }
    // The rect of the screen the page wants uncovered at `p` (the 3D preview). False: none.
    virtual bool Hole(const Pos&,skin::Rect*) const noexcept { return false; }
    // A click on something the page drew (its own tabs / buttons); it may move `p`. True: taken.
    virtual bool Click(Pos& /*p*/,float /*x*/,float /*y*/) noexcept { return false; }
    // Z / C on a page without sub tabs. True when something changed.
    virtual bool Cycle(const Pos&,int /*delta*/) noexcept { return false; }
protected:
    ~Page()=default;
};

// ---- input / effects -------------------------------------------------------------------------------------------------
enum Key : int {
    kUp,kDown,kLeft,kRight,kConfirm,kCancel,kTabPrev,kTabNext,kSubPrev,kSubNext,kStore,kPresetMenu,kBackspace,
    kToggle,kMouseLeft,kMouseRight,kDigit1,kDigit8=kDigit1+7,kKeyCount
};
struct Input {
    bool held[kKeyCount]{};
    bool pointer=false;   // x / y valid (the cursor over the window, design units)
    float x=0,y=0;
    int wheel=0;          // notches since the last frame, + away from the user (up)
    wchar_t typed[8]{};   // characters typed since the last frame (WM_CHAR), for a rename
    int ntyped=0;
};

// The preset chips as the runtime knows them this frame.
struct Presets {
    int count=0;                              // 0..kPresetMost
    int current=-1;                           // 0-based, -1 none
    wchar_t names[kPresetMost][kTextMost]{};
};

// What the runtime does after Step (at most one preset operation per frame).
enum class Op : std::uint8_t { none,switchTo,store,add,copy,rename,remove };
struct Effects {
    bool close=false;
    Op op=Op::none;
    int index=-1;                  // the preset (switchTo, store, copy, rename, remove)
    wchar_t name[kTextMost]{};     // add / copy / rename
};

// Localized words (hudtext), given by the runtime.
struct Words {
    const wchar_t* title=L"";
    const wchar_t* add=L"+";
    const wchar_t* actions[5]{};      // 新建, 复制, 改名, 删除, 存入
    const wchar_t* confirmDelete=L"";
    const wchar_t* renameTitle=L"";
    const wchar_t* hints[5]{};        // list, centre, details, popup, rename
    const wchar_t* suggest[kSuggestMost]{};   // names a pad cycles for a rename / new preset
    int nsuggest=0;
    const wchar_t* unsaved=L"*";              // the hint bar's mark of a page with edits not yet written
    const wchar_t* unsavedTitle=L"";          // the close prompt's question
    const wchar_t* unsavedActions[3]{};       // save and close, discard and close, back
};

enum class Popup : std::uint8_t { none,actions,confirmDelete,rename,unsaved };
enum PopupAction : int { kActNew,kActCopy,kActRename,kActDelete,kActStore,kActCount };
enum UnsavedAction : int { kUnsavedSave,kUnsavedDiscard,kUnsavedBack,kUnsavedCount };

struct State {
    bool open=false;
    int tab=0;
    int column=0;
    Pos pos;
    int top[3]{};                 // first visible line of each column
    Popup popup=Popup::none;
    int popupCursor=0;
    int popupTarget=-1;           // the preset the popup acts on
    int renameAction=kActRename;  // kActNew / kActCopy / kActRename: what the typed name is for
    wchar_t renameText[kTextMost]{};
    int suggestCursor=-1;
    bool prev[kKeyCount]{};
    wchar_t note[kTextMost]{};
    int noteFrames=0;
};

inline constexpr int kNoteFrames=240;

inline void Note(State& s,const wchar_t* text) noexcept {
    CopyText(s.note,kTextMost,text);
    s.noteFrames=kNoteFrames;
}

// Open: the keys held now count as already down (the opening key does not fire again).
inline void Open(State& s,const Input& in,int tab=0) noexcept {
    const int keepTab=tab;
    s=State{};
    s.open=true;
    s.tab=keepTab;
    for(int k=0;k<kKeyCount;++k)s.prev[k]=in.held[k];
}
inline void Close(State& s) noexcept {
    s.open=false;
    s.popup=Popup::none;
}

namespace detail {
inline int Clamp(int v,int lo,int hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
inline int Wrap(int v,int n) noexcept { return n<=0 ? 0 : ((v%n)+n)%n; }
inline int FirstListRow(const Page& p) noexcept { return p.Subs()>0 ? 1 : 0; }
// Rows the page's reserved top of `column` takes from it (whole rows: the rows keep their pitch).
inline int TopRows(const Page& p,int column) noexcept {
    const float top=p.ColumnTop(column);
    if(top<=0)return 0;
    const float pitch=skin::layout::kRowHeight+skin::layout::kRowGap;
    return static_cast<int>((top+pitch-1)/pitch);
}
inline int Visible(const Page& p,int column) noexcept {
    const int n=skin::layout::RowsIn(column)-(column==0 ? FirstListRow(p) : 0)-TopRows(p,column);
    return n>1 ? n : 1;
}
// The rect of visible line `v` of `column` (under the sub tabs and the page's reserved top).
inline skin::Rect LineRect(const Page& p,int column,int v) noexcept {
    return skin::layout::Row(column,v+(column==0 ? FirstListRow(p) : 0)+TopRows(p,column));
}
inline int Count(const Page& p,const Pos& pos,int column) noexcept {
    return column==0 ? p.Rows(pos.sub) : column==1 ? p.Items(pos) : p.Fields(pos);
}
inline int& Index(Pos& pos,int column) noexcept { return column==0 ? pos.row : column==1 ? pos.item : pos.field; }
inline int IndexOf(const Pos& pos,int column) noexcept { return column==0 ? pos.row : column==1 ? pos.item : pos.field; }

// Every index inside its count (counts change under the cursor), the column one that has lines, the scroll showing
// the cursor.
inline void Clamp(State& s,const Page& p) noexcept {
    const int subs=p.Subs();
    s.pos.sub=subs>0 ? Clamp(s.pos.sub,0,subs-1) : 0;
    for(int c=0;c<3;++c){
        const int n=Count(p,s.pos,c);
        int& i=Index(s.pos,c);
        i=n>0 ? Clamp(i,0,n-1) : 0;
    }
    while(s.column>0 && Count(p,s.pos,s.column)<=0)--s.column;
    for(int c=0;c<3;++c){
        const int vis=Visible(p,c), n=Count(p,s.pos,c), i=Index(s.pos,c);
        if(i<s.top[c])s.top[c]=i;
        if(i>=s.top[c]+vis)s.top[c]=i-vis+1;
        s.top[c]=Clamp(s.top[c],0,n>vis ? n-vis : 0);
    }
}

inline void MoveIn(State& s,const Page& p,int delta) noexcept {
    const int n=Count(p,s.pos,s.column);
    if(n<=0)return;
    int& i=Index(s.pos,s.column);
    i=Wrap(i+delta,n);
    if(s.column==0){s.pos.item=0;s.pos.field=0;s.top[1]=s.top[2]=0;}
    if(s.column==1){s.pos.field=0;s.top[2]=0;}
}

// Commit the page's edits; on failure show why and say so.
inline bool CommitPage(State& s,Page& p) noexcept {
    wchar_t why[kTextMost]{};
    if(p.Commit(why,kTextMost))return true;
    Note(s,why[0] ? why : L"!");
    return false;
}

inline bool Pressed(const State& s,const Input& in,int k) noexcept { return in.held[k] && !s.prev[k]; }

// The chip index under (x, y): 0..count-1 a preset, count the ＋ (when there is room), -1 none.
inline int ChipAt(const Presets& ps,float x,float y) noexcept {
    const int chips=ps.count+(ps.count<kPresetMost ? 1 : 0);
    for(int i=0;i<chips;++i)
        if(skin::layout::Chip(i,chips).Contains(x,y))return i;
    return -1;
}
inline int TabAt(int pages,float x,float y) noexcept {
    for(int i=0;i<pages;++i)
        if(skin::layout::Tab(i).Contains(x,y))return i;
    return -1;
}
// The popup's box and its lines.
inline skin::Rect PopupBox() noexcept { return skin::Rect{720,300,1200,300+60+kActCount*60.0f+20}; }
inline skin::Rect PopupLine(int i) noexcept {
    const skin::Rect b=PopupBox();
    const float y=b.y0+60+static_cast<float>(i)*60.0f;
    return skin::Rect{b.x0+30,y,b.x1-30,y+50};
}
inline skin::Rect SubTab(const Page& p,int i) noexcept {
    const skin::Rect r=skin::layout::Row(0,0);
    const int n=p.Subs()>0 ? p.Subs() : 1;
    const float w=r.W()/static_cast<float>(n);
    return skin::Rect{r.x0+w*static_cast<float>(i),r.y0,r.x0+w*static_cast<float>(i+1)-4,r.y1};
}

inline bool AppendChar(wchar_t* text,wchar_t c) noexcept {
    std::size_t n=0;
    while(n<kTextMost && text[n])++n;
    if(n+1>=kTextMost || c<0x20 || c==0x7F)return false;
    text[n]=c;text[n+1]=0;
    return true;
}
inline void PopChar(wchar_t* text) noexcept {
    std::size_t n=0;
    while(n<kTextMost && text[n])++n;
    if(n>0)text[n-1]=0;
}

inline void StartRename(State& s,const Presets& ps,int action,int target) noexcept {
    s.popup=Popup::rename;
    s.renameAction=action;
    s.popupTarget=target;
    s.suggestCursor=-1;
    if(action==kActNew)s.renameText[0]=0;
    else CopyText(s.renameText,kTextMost,target>=0 && target<ps.count ? ps.names[target] : L"");
}

inline skin::Rect UnsavedLine(int i) noexcept { return PopupLine(i+1); }
// The close prompt (Popup::unsaved): save and close, discard and close, back.
inline void UnsavedStep(State& s,Page& p,const Input& in,Effects* fx) noexcept {
    int pick=-1;
    if(Pressed(s,in,kUp))s.popupCursor=Wrap(s.popupCursor-1,kUnsavedCount);
    if(Pressed(s,in,kDown))s.popupCursor=Wrap(s.popupCursor+1,kUnsavedCount);
    if(in.wheel)s.popupCursor=Wrap(s.popupCursor-in.wheel,kUnsavedCount);
    if(Pressed(s,in,kConfirm))pick=s.popupCursor;
    if(Pressed(s,in,kMouseLeft) && in.pointer){
        for(int i=0;i<kUnsavedCount;++i)
            if(UnsavedLine(i).Contains(in.x,in.y)){pick=i;s.popupCursor=i;}
        if(pick<0 && !PopupBox().Contains(in.x,in.y))pick=kUnsavedBack;
    }
    if(Pressed(s,in,kCancel))pick=kUnsavedBack;
    switch(pick){
    case kUnsavedSave:
        s.popup=Popup::none;
        if(CommitPage(s,p)){fx->close=true;Close(s);}
        break;
    case kUnsavedDiscard:
        p.Discard();
        fx->close=true;
        Close(s);
        break;
    case kUnsavedBack:
        s.popup=Popup::none;
        break;
    default:
        break;
    }
}
// Close asked: a page with edits asks first; otherwise commit (a page may write what is not an edit) and close.
inline void AskClose(State& s,Page& p,Effects* fx) noexcept {
    if(p.Dirty()){s.popup=Popup::unsaved;s.popupCursor=kUnsavedSave;return;}
    if(CommitPage(s,p)){fx->close=true;Close(s);}
}

inline void PopupStep(State& s,const Input& in,const Presets& ps,const Words& w,Effects* fx) noexcept {
    // the book may have changed under the popup (an operation of the last frame): its target must still be there, a
    // new / copied preset must still have room
    const bool target=s.popupTarget>=0 && s.popupTarget<ps.count;
    const bool adds=s.popup==Popup::rename && s.renameAction!=kActRename;
    if((s.popup!=Popup::actions && !target && !(adds && s.renameAction==kActNew)) || (adds && ps.count>=kPresetMost)){
        s.popup=Popup::none;
        return;
    }
    if(s.popup==Popup::actions && !target)s.popupTarget=-1;
    if(s.popup==Popup::rename){
        for(int i=0;i<in.ntyped && i<8;++i)AppendChar(s.renameText,in.typed[i]);
        if(Pressed(s,in,kBackspace))PopChar(s.renameText);
        if(w.nsuggest>0 && (Pressed(s,in,kUp) || Pressed(s,in,kDown))){
            s.suggestCursor=Wrap(s.suggestCursor+(Pressed(s,in,kUp) ? -1 : 1),w.nsuggest);
            CopyText(s.renameText,kTextMost,w.suggest[s.suggestCursor]);
        }
        if(Pressed(s,in,kCancel)){s.popup=Popup::none;return;}
        if(Pressed(s,in,kConfirm) && s.renameText[0]){
            fx->op=s.renameAction==kActNew ? Op::add : s.renameAction==kActCopy ? Op::copy : Op::rename;
            fx->index=s.popupTarget;
            CopyText(fx->name,kTextMost,s.renameText);
            s.popup=Popup::none;
        }
        return;
    }
    if(s.popup==Popup::confirmDelete){
        if(Pressed(s,in,kCancel)){s.popup=Popup::actions;return;}
        if(Pressed(s,in,kConfirm)){
            fx->op=Op::remove;
            fx->index=s.popupTarget;
            s.popup=Popup::none;
        }
        return;
    }
    // actions
    int pick=-1;
    if(Pressed(s,in,kUp))s.popupCursor=Wrap(s.popupCursor-1,kActCount);
    if(Pressed(s,in,kDown))s.popupCursor=Wrap(s.popupCursor+1,kActCount);
    if(in.wheel)s.popupCursor=Wrap(s.popupCursor-in.wheel,kActCount);
    if(Pressed(s,in,kConfirm))pick=s.popupCursor;
    if(Pressed(s,in,kMouseLeft) && in.pointer){
        pick=-2;
        for(int i=0;i<kActCount;++i)
            if(PopupLine(i).Contains(in.x,in.y)){pick=i;s.popupCursor=i;}
        if(pick==-2 && !PopupBox().Contains(in.x,in.y)){s.popup=Popup::none;return;}
    }
    if(Pressed(s,in,kCancel) || Pressed(s,in,kPresetMenu)){s.popup=Popup::none;return;}
    const bool have=s.popupTarget>=0 && s.popupTarget<ps.count;
    switch(pick){
    case kActNew:
        if(ps.count<kPresetMost)StartRename(s,ps,kActNew,-1);
        break;
    case kActCopy:
        if(have && ps.count<kPresetMost)StartRename(s,ps,kActCopy,s.popupTarget);
        break;
    case kActRename:
        if(have)StartRename(s,ps,kActRename,s.popupTarget);
        break;
    case kActDelete:
        if(have)s.popup=Popup::confirmDelete;
        break;
    case kActStore:
        if(have){fx->op=Op::store;fx->index=s.popupTarget;s.popup=Popup::none;}
        break;
    default:
        break;
    }
}

// Mouse in the columns: focus what was clicked; a second click on the focused field activates it.
inline void ClickColumns(State& s,Page& p,const Input& in) noexcept {
    if(FirstListRow(p)>0)
        for(int i=0;i<p.Subs();++i)
            if(SubTab(p,i).Contains(in.x,in.y)){
                if(s.pos.sub!=i){s.pos=Pos{i,0,0,0};s.top[0]=s.top[1]=s.top[2]=0;}
                s.column=0;
                return;
            }
    for(int c=0;c<3;++c){
        const int n=Count(p,s.pos,c), vis=Visible(p,c);
        for(int v=0;v<vis && s.top[c]+v<n;++v){
            if(!LineRect(p,c,v).Contains(in.x,in.y))continue;
            const int i=s.top[c]+v;
            int& at=Index(s.pos,c);
            if(c==2 && s.column==2 && at==i){p.Activate(s.pos);return;}
            if(c==1 && s.column==1 && at==i && p.Fields(s.pos)<=0){p.Activate(s.pos);return;}
            if(at!=i){
                at=i;
                if(c==0){s.pos.item=0;s.pos.field=0;s.top[1]=s.top[2]=0;}
                if(c==1){s.pos.field=0;s.top[2]=0;}
            }
            s.column=c;
            return;
        }
    }
}
}  // namespace detail

// One UI frame. `pages` / `npages`: the screen's tabs (at least one). Nothing happens while closed.
inline Effects Step(State& s,Page* const* pages,int npages,const Presets& ps,const Words& w,const Input& in) noexcept {
    using namespace detail;
    Effects fx;
    if(!s.open || npages<=0)return fx;
    s.tab=Clamp(s.tab,0,npages-1);
    Page& page=*pages[s.tab];
    Clamp(s,page);
    if(s.noteFrames>0 && --s.noteFrames==0)s.note[0]=0;

    if(Pressed(s,in,kToggle) && s.popup==Popup::unsaved){
        s.popup=Popup::none;
    }else if(Pressed(s,in,kToggle) && s.popup!=Popup::rename){
        s.popup=Popup::none;
        AskClose(s,page,&fx);
    }else if(s.popup==Popup::unsaved){
        UnsavedStep(s,page,in,&fx);
    }else if(s.popup!=Popup::none){
        PopupStep(s,in,ps,w,&fx);
    }else{
        // preset chips: keys and mouse
        for(int d=0;d<kPresetMost;++d)
            if(Pressed(s,in,kDigit1+d) && d<ps.count && CommitPage(s,page)){fx.op=Op::switchTo;fx.index=d;}
        if(Pressed(s,in,kStore) && ps.current>=0 && CommitPage(s,page)){fx.op=Op::store;fx.index=ps.current;}
        if(Pressed(s,in,kPresetMenu) && CommitPage(s,page)){
            s.popup=Popup::actions;
            s.popupCursor=ps.count>0 ? kActStore : kActNew;
            s.popupTarget=ps.current;
        }
        if(in.pointer && (Pressed(s,in,kMouseLeft) || Pressed(s,in,kMouseRight))){
            const bool left=Pressed(s,in,kMouseLeft);
            const int chip=ChipAt(ps,in.x,in.y), tab=TabAt(npages,in.x,in.y);
            if(chip>=0){
                if(CommitPage(s,page)){
                    if(chip>=ps.count){if(left)StartRename(s,ps,kActNew,-1);}
                    else if(left){fx.op=Op::switchTo;fx.index=chip;}
                    else{s.popup=Popup::actions;s.popupCursor=kActStore;s.popupTarget=chip;}
                }
            }else if(tab>=0 && left){
                if(tab!=s.tab && CommitPage(s,page)){s.tab=tab;s.column=0;s.pos=Pos{};s.top[0]=s.top[1]=s.top[2]=0;}
            }else if(left){
                if(!page.Click(s.pos,in.x,in.y))ClickColumns(s,page,in);
            }
        }
        // page tabs, sub tabs
        const int dt=(Pressed(s,in,kTabNext) ? 1 : 0)-(Pressed(s,in,kTabPrev) ? 1 : 0);
        if(dt && npages>1 && fx.op==Op::none && s.popup==Popup::none && CommitPage(s,page)){
            s.tab=Wrap(s.tab+dt,npages);
            s.column=0;s.pos=Pos{};s.top[0]=s.top[1]=s.top[2]=0;
        }
        Page& now=*pages[s.tab];
        const int ds=(Pressed(s,in,kSubNext) ? 1 : 0)-(Pressed(s,in,kSubPrev) ? 1 : 0);
        if(ds && now.Subs()>1 && s.popup==Popup::none){
            s.pos=Pos{Wrap(s.pos.sub+ds,now.Subs()),0,0,0};
            s.column=0;s.top[0]=s.top[1]=s.top[2]=0;
        }else if(ds && now.Subs()<=1 && s.popup==Popup::none){
            now.Cycle(s.pos,ds);
        }
        // columns
        if(s.popup==Popup::none && fx.op==Op::none){
            const int dy=(Pressed(s,in,kDown) ? 1 : 0)-(Pressed(s,in,kUp) ? 1 : 0);
            if(dy)MoveIn(s,now,dy);
            if(in.wheel){
                if(s.column==2)now.Step(s.pos,in.wheel>0 ? 1 : -1);
                else MoveIn(s,now,-in.wheel);
            }
            if(s.column==2){
                if(Pressed(s,in,kLeft))now.Step(s.pos,-1);
                if(Pressed(s,in,kRight))now.Step(s.pos,1);
                if(Pressed(s,in,kConfirm))now.Activate(s.pos);
                if(Pressed(s,in,kCancel))s.column=1;
            }else{
                const bool deeper=Count(now,s.pos,s.column+1)>0;
                if((Pressed(s,in,kRight) || Pressed(s,in,kConfirm)) && deeper)++s.column;
                else if(Pressed(s,in,kConfirm) && s.column==1)now.Activate(s.pos);   // an item that acts itself
                if(Pressed(s,in,kLeft) && s.column>0)--s.column;
                if(Pressed(s,in,kCancel)){
                    if(s.column>0)--s.column;
                    else AskClose(s,now,&fx);
                }
            }
        }
    }
    for(int k=0;k<kKeyCount;++k)s.prev[k]=in.held[k];
    if(s.open)Clamp(s,*pages[s.tab]);
    return fx;
}

// The frame's picture of the screen (the UI thread builds it, the render thread paints it: src/armory_draw.cpp).
inline void Build(const State& s,Page* const* pages,int npages,const Presets& ps,const Words& w,View* out) noexcept {
    using namespace detail;
    namespace L=skin::layout;
    out->Clear();
    if(!s.open || npages<=0)return;
    out->open=true;
    Layer& base=out->layer[0];
    const Page& page=*pages[Clamp(s.tab,0,npages-1)];
    skin::Diamonds(&base.quads,skin::Rect{0,0,skin::kWidth,skin::kHeight});
    skin::Bar(&base.quads,L::kTitle);
    skin::Bar(&base.quads,L::kHint);
    base.Say(w.title,L::kTitle.x0+60,L::kTitle.y0+14,36,skin::kTextDark,Align::left,true,L::kTabX-L::kTitle.x0-70);
    for(int i=0;i<npages && i<L::kTabMost;++i){
        const skin::Rect r=L::Tab(i);
        skin::Tab(&base.quads,r,i==s.tab);
        base.Say(pages[i]->Name(),(r.x0+r.x1)/2,r.y0+12,26,skin::TextOn(i==s.tab),Align::centre,i==s.tab,r.W()-24);
        if(pages[i]->Dirty())   // the unsaved mark: an orange corner on the tab
            base.quads.Add(r.x1-24,r.y0,r.x1,r.y0,r.x1,r.y0+24,r.x1,r.y0+24,skin::kCursor,skin::kCursor,skin::kCursor,
                           skin::kCursor);
    }
    const int chips=ps.count+(ps.count<kPresetMost ? 1 : 0);
    for(int i=0;i<chips;++i){
        const skin::Rect r=L::Chip(i,chips);
        const bool preset=i<ps.count, current=i==ps.current;
        skin::Chip(&base.quads,r,current,s.popup!=Popup::none && i==s.popupTarget);
        wchar_t label[4]{};
        label[0]=preset ? static_cast<wchar_t>(L'1'+i) : 0;
        base.Say(preset ? label : w.add,(r.x0+r.x1)/2,r.y0+8,24,skin::TextOn(current),Align::centre,current);
    }
    if(ps.current>=0 && ps.current<ps.count){
        const skin::Rect first=L::Chip(0,chips);
        base.Say(ps.names[ps.current],first.x0-12,first.y0+8,24,skin::kTextDark,Align::right,true,
                 first.x0-L::Tab(npages<L::kTabMost ? npages : L::kTabMost).x0-24);
    }
    // columns
    for(int c=0;c<3;++c)skin::Panel(&base.quads,L::Column(c));
    const int first=FirstListRow(page);
    if(first>0)
        for(int i=0;i<page.Subs();++i){
            const skin::Rect r=SubTab(page,i);
            skin::Tab(&base.quads,r,i==s.pos.sub);
            base.Say(page.SubName(i),(r.x0+r.x1)/2,r.y0+12,24,skin::TextOn(i==s.pos.sub),Align::centre,false,r.W()-12);
        }
    // what is painted around the page's hole: everything so far (the background, the bars, the panels)
    {
        skin::Rect hole{};
        if(page.Hole(s.pos,&hole) && hole.W()>0 && hole.H()>0){base.hole=hole;base.holeQuads=base.quads.n;}
    }
    for(int c=0;c<3;++c){
        if(c>s.column+1 || (c>0 && Count(page,s.pos,c)<=0))continue;
        const int n=Count(page,s.pos,c), vis=Visible(page,c);
        for(int v=0;v<vis && s.top[c]+v<n;++v){
            Pos p=s.pos;
            const int i=s.top[c]+v;
            Index(p,c)=i;
            Line line;
            if(c==0)page.RowLine(p.sub,i,&line);
            else if(c==1)page.ItemLine(p,&line);
            else page.FieldLine(p,&line);
            const skin::Rect r=LineRect(page,c,v);
            const bool at=IndexOf(s.pos,c)==i;
            const bool beige=c==0 && at;
            if(c==0)skin::Button(&base.quads,r,beige);
            else skin::Cell(&base.quads,r);
            const std::uint32_t col=line.dim ? skin::kTextDim : skin::TextOn(beige);
            const float textW=line.value[0] ? r.W()*0.5f : r.W()-40;
            base.Say(line.label,r.x0+22,r.y0+12,26,col,Align::left,beige,textW);
            if(line.value[0]){
                const bool arrows=line.steps && c==2 && at && s.column==2;
                wchar_t v2[kTextMost]{};
                if(arrows){
                    v2[0]=L'\x25C0';v2[1]=L' ';
                    CopyText(v2+2,kTextMost-4,line.value);
                    std::size_t k=0;
                    while(v2[k])++k;
                    v2[k]=L' ';v2[k+1]=L'\x25B6';v2[k+2]=0;
                }
                base.Say(arrows ? v2 : line.value,r.x1-22,r.y0+12,26,col,Align::right,false,r.W()*0.5f-30);
            }
            if(line.chip){   // the colour swatch left of the value, edged so a dark colour still shows
                const float x=r.x0+r.W()*0.5f-40, y=r.y0+11;
                base.quads.Box(skin::Rect{x-2,y-2,x+32,y+32},skin::kCellEdge);
                base.quads.Box(skin::Rect{x,y,x+30,y+30},line.chip);
            }
            if(at && c==s.column && s.popup==Popup::none)skin::Cursor(&base.quads,r);
        }
        page.Draw(s.pos,c,&base);
    }
    // hint bar, note
    const int hint=s.popup==Popup::rename ? 4 : s.popup!=Popup::none ? 3 : s.column;
    base.Say(w.hints[hint],L::kHint.x0+60,L::kHint.y0+12,24,skin::kTextDark,Align::left,false,L::kHint.W()-560);
    if(s.note[0])base.Say(s.note,L::kHint.x1-60,L::kHint.y0+12,24,0xFF8A1E0Au,Align::right,true,440);
    else if(page.Dirty())base.Say(w.unsaved,L::kHint.x1-60,L::kHint.y0+12,24,0xFF8A1E0Au,Align::right,true,440);
    // popup
    if(s.popup==Popup::none)return;
    Layer& top=out->layer[1];
    top.quads.Box(skin::Rect{0,0,skin::kWidth,skin::kHeight},0x90000000u);
    const skin::Rect box=PopupBox();
    skin::Panel(&top.quads,box);
    const wchar_t* target=s.popupTarget>=0 && s.popupTarget<ps.count ? ps.names[s.popupTarget] : L"";
    if(s.popup==Popup::actions){
        top.Say(target,(box.x0+box.x1)/2,box.y0+16,28,skin::kText,Align::centre,true,box.W()-40);
        for(int i=0;i<kActCount;++i){
            const skin::Rect r=PopupLine(i);
            const bool at=i==s.popupCursor;
            skin::Button(&top.quads,r,at);
            top.Say(w.actions[i],(r.x0+r.x1)/2,r.y0+12,26,skin::TextOn(at),Align::centre,at);
            if(at)skin::Cursor(&top.quads,r);
        }
    }else if(s.popup==Popup::unsaved){
        top.Say(w.unsavedTitle,(box.x0+box.x1)/2,box.y0+16,28,skin::kText,Align::centre,true,box.W()-40);
        for(int i=0;i<kUnsavedCount;++i){
            const skin::Rect r=UnsavedLine(i);
            const bool at=i==s.popupCursor;
            skin::Button(&top.quads,r,at);
            top.Say(w.unsavedActions[i],(r.x0+r.x1)/2,r.y0+12,26,skin::TextOn(at),Align::centre,at);
            if(at)skin::Cursor(&top.quads,r);
        }
    }else if(s.popup==Popup::confirmDelete){
        top.Say(target,(box.x0+box.x1)/2,box.y0+16,28,skin::kText,Align::centre,true,box.W()-40);
        top.Say(w.confirmDelete,(box.x0+box.x1)/2,box.y0+120,28,skin::kCursor,Align::centre,true,box.W()-40);
    }else{
        top.Say(w.renameTitle,(box.x0+box.x1)/2,box.y0+16,28,skin::kText,Align::centre,true,box.W()-40);
        const skin::Rect r=PopupLine(1);
        skin::Cell(&top.quads,r);
        skin::Cursor(&top.quads,r);
        wchar_t shown[kTextMost]{};
        CopyText(shown,kTextMost-1,s.renameText);
        AppendChar(shown,L'_');
        top.Say(shown,r.x0+18,r.y0+12,26,skin::kText,Align::left,false,r.W()-36);
    }
}

}  // namespace crew::armory

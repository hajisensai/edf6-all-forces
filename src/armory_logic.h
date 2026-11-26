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
// the background and the panels are cut round it here (skin::DiamondsAround / PanelAround) and every quad before the
// lines (Layer::holeQuads) is painted clipped round it (src/armory_draw.cpp).
// A page may also take keys first (Page::Key), take typed text (Page::Typing / Type: its search box), reserve header
// lines (Page::Header), move the cursor after its lines change (Page::Relocate), post a note (Page::TakeNote) and
// replace the hint bar's text (Page::Hint).
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
// maxW > 0: at most that wide, fitted (smaller, then cut with an ellipsis: src/text_fit.h, ui_text.h Fit). A text that
// yields (yieldTo: an index of the same layer, drawn before it) gets only what that one leaves of maxW: its room ends
// `gap` before the other's drawn edge (a row's label before its value, the hint bar's keys before the note). size2 > 0:
// it may take two rows of that size, their tops at rowY.
struct Text {
    wchar_t s[kTextMost]{};
    float x=0,y=0,size=24,maxW=0;
    std::uint32_t argb=skin::kText;
    Align align=Align::left;
    bool bold=false;
    std::int16_t yieldTo=-1;
    float gap=0,size2=0;
    float rowY[2]{};
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
    // The text's index (-1: the layer is full).
    int Say(const wchar_t* s,float x,float y,float size,std::uint32_t argb,Align a=Align::left,bool bold=false,
            float maxW=0) noexcept {
        if(nt>=kTextMost){++droppedTexts;return -1;}
        Text& t=texts[nt++];
        CopyText(t.s,armory::kTextMost,s);
        t.x=x;t.y=y;t.size=size;t.argb=argb;t.align=a;t.bold=bold;t.maxW=maxW;
        return nt-1;
    }
    // Text `i` gives way to text `to` (both said; `to` first wins its width), `gap` between them.
    void Yield(int i,int to,float gap) noexcept {
        if(i<0 || i>=nt || to<0 || to>=nt || to==i)return;
        texts[i].yieldTo=static_cast<std::int16_t>(to);texts[i].gap=gap;
    }
    // Text `i` may take two rows `size2` tall, their tops at y0 / y1.
    void TwoRows(int i,float size2,float y0,float y1) noexcept {
        if(i<0 || i>=nt)return;
        texts[i].size2=size2;texts[i].rowY[0]=y0;texts[i].rowY[1]=y1;
    }
};
// A text's horizontal extent as drawn (none: nothing drawn).
struct Edge { bool on=false; float x0=0,x1=0; };
inline Edge EdgeOf(const Text& t,float w) noexcept {
    if(!(w>0.f))return Edge{};
    const float x0=t.align==Align::left ? t.x : t.align==Align::right ? t.x-w : t.x-w*0.5f;
    return Edge{true,x0,x0+w};
}
// The width text `t` may take beside `other` (drawn): its maxW, less what reaches into it from the other side.
inline float RoomBeside(const Text& t,const Edge& other) noexcept {
    float room=t.maxW;
    if(other.on) {
        float until=room;
        if(t.align==Align::left && other.x0>t.x)until=other.x0-t.gap-t.x;
        else if(t.align==Align::right && other.x1<t.x)until=t.x-(other.x1+t.gap);
        if(!(room>0.f) || until<room)room=until;
    }
    return room;
}
// Two layers: the screen, then the popup over it (quads of a layer under its texts).
struct View {
    Layer layer[2];
    bool open=false;
    void Clear() noexcept {
        for(Layer& l:layer){l.quads.n=0;l.quads.dropped=0;l.nt=0;l.droppedTexts=0;l.hole=skin::Rect{};l.holeQuads=0;}
        open=false;
    }
};

// ---- input ------------------------------------------------------------------------------------------------------------
// kAltPrev / kAltNext, kJumpPrev / kJumpNext, kSearch mean nothing to the state itself: a page takes them (Page::Key),
// e.g. the weapons page's category, letter jump and search box.
enum Key : int {
    kUp,kDown,kLeft,kRight,kConfirm,kCancel,kTabPrev,kTabNext,kSubPrev,kSubNext,kStore,kPresetMenu,kBackspace,
    kToggle,kAltPrev,kAltNext,kJumpPrev,kJumpNext,kSearch,kMouseLeft,kMouseRight,kDigit1,kDigit8=kDigit1+7,kKeyCount
};
struct Input {
    bool held[kKeyCount]{};
    bool pointer=false;   // x / y valid (the cursor over the window, design units)
    float x=0,y=0;
    int wheel=0;          // notches since the last frame, + away from the user (up)
    wchar_t typed[8]{};   // characters typed since the last frame (WM_CHAR), for a rename / a page's search
    int ntyped=0;
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
    // Lines at the top of a column the page draws itself (Draw; layout::Row(column, first..) below the sub tabs of the
    // list): the state's lines start after them. Constant for a page.
    virtual int Header(int /*column*/) const noexcept { return 0; }
    // A rect of the screen nothing may cover (the 3D view: room_preview.h SetViewPlacement): the background and the
    // panels are drawn round it. Inside the details column, above its lines (Header(2) keeps them clear of it).
    virtual bool Hole(const Pos&,skin::Rect* /*out*/) const noexcept { return false; }
    // A key that went down this frame (kToggle excepted), before the state uses it: true takes it (the state does
    // nothing with it this frame). The pointer is in `in` (a click on a header line). The page may move the cursor.
    virtual bool Key(Pos&,int /*column*/,int /*key*/,const Input&) noexcept { return false; }
    // The page is taking typed text (its search box): only Type and Key run, the state's keys do nothing.
    virtual bool Typing() const noexcept { return false; }
    virtual void Type(const wchar_t* /*chars*/,int /*n*/) noexcept {}
    // Last in a frame, before the cursor is clamped: the page may put the cursor somewhere else (its lines rebuilt).
    virtual void Relocate(Pos*,int /*column*/) noexcept {}
    // Something to show in the note slot (why an equip was refused): copy it and say true, once.
    virtual bool TakeNote(wchar_t* /*out*/,std::size_t /*cap*/) noexcept { return false; }
    // The hint bar text instead of Words::hints[column]; nullptr keeps that.
    virtual const wchar_t* Hint(const Pos&,int /*column*/) const noexcept { return nullptr; }
    // Design units reserved at the top of column 0..2 (its rows start below, after Header's lines; Draw fills it).
    // 0: none.
    virtual float ColumnTop(int /*column*/) const noexcept { return 0; }
    // A click on something the page drew (its own tabs / buttons); it may move `p`. True: taken.
    virtual bool Click(Pos& /*p*/,float /*x*/,float /*y*/) noexcept { return false; }
    // Z / C on a page without sub tabs. True when something changed.
    virtual bool Cycle(const Pos&,int /*delta*/) noexcept { return false; }
protected:
    ~Page()=default;
};

// ---- effects -------------------------------------------------------------------------------------------------------
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
    bool backToTasks=false;
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
    const wchar_t* rootExit=L"";      // the parent screen's action when Escape is pressed in the left column
    const wchar_t* deleteHint=L"";
    const wchar_t* suggest[kSuggestMost]{};   // names a pad cycles for a rename / new preset
    int nsuggest=0;
    const wchar_t* unsaved=L"*";              // the hint bar's mark of a page with edits not yet written
    const wchar_t* unsavedTitle=L"";          // the close prompt's question
    const wchar_t* unsavedActions[3]{};       // save and close, discard and close, back
};

// The status strip under the hint bar (the old room menu's status lines): the start held back and why, the last
// save's result, the variant generator, the host's settings, the room's sync, the PvP wait. The runtime fills it each
// frame (armory.cpp FillStatus); at most kStatusMost, in the order added, a warning drawn orange-red.
inline constexpr int kStatusMost=4;
struct StatusLines {
    wchar_t text[kStatusMost][kTextMost]{};
    bool warn[kStatusMost]{};
    int n=0;
    void Add(const wchar_t* line,bool isWarn) noexcept {
        if(n>=kStatusMost || !line || !*line)return;
        CopyText(text[n],kTextMost,line);
        warn[n++]=isWarn;
    }
};
inline constexpr std::uint32_t kStatusWarn=0xFFE05A3Cu;

enum class Popup : std::uint8_t { none,actions,confirmDelete,rename,unsaved };
enum PopupAction : int { kActNew,kActCopy,kActRename,kActDelete,kActStore,kActCount };
enum UnsavedAction : int { kUnsavedSave,kUnsavedDiscard,kUnsavedBack,kUnsavedCount };

struct State {
    bool open=false;
    bool backToTasks=false;
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
// The first layout row of a column the state's lines use (sub tabs, the page's header lines, its reserved top).
inline int FirstRow(const Page& p,int column) noexcept {
    const int h=p.Header(column);
    return (column==0 ? FirstListRow(p) : 0)+(h>0 ? h : 0)+TopRows(p,column);
}
inline int Visible(const Page& p,int column) noexcept {
    const int v=skin::layout::RowsIn(column)-FirstRow(p,column);
    return v>1 ? v : 1;
}
// The rect of visible line `v` of `column` (under the sub tabs, the page's header and its reserved top).
inline skin::Rect LineRect(const Page& p,int column,int v) noexcept {
    return skin::layout::Row(column,v+FirstRow(p,column));
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
inline skin::Rect PopupBox() noexcept { return skin::Rect{660,300,1260,300+60+kActCount*60.0f+44}; }
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
        if(CommitPage(s,p)){fx->close=true;fx->backToTasks=s.backToTasks;Close(s);}
        break;
    case kUnsavedDiscard:
        p.Discard();
        fx->close=true;
        fx->backToTasks=s.backToTasks;
        Close(s);
        break;
    case kUnsavedBack:
        s.popup=Popup::none;
        s.backToTasks=false;
        break;
    default:
        break;
    }
}
// Close asked: a page with edits asks first; otherwise commit (a page may write what is not an edit) and close.
inline void AskClose(State& s,Page& p,Effects* fx,bool backToTasks=false) noexcept {
    s.backToTasks=backToTasks;
    if(p.Dirty()){s.popup=Popup::unsaved;s.popupCursor=kUnsavedSave;return;}
    if(CommitPage(s,p)){fx->close=true;fx->backToTasks=backToTasks;Close(s);}
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
        s.backToTasks=false;
    }else if(Pressed(s,in,kToggle) && s.popup!=Popup::rename){
        s.popup=Popup::none;
        AskClose(s,page,&fx);
    }else if(s.popup==Popup::unsaved){
        UnsavedStep(s,page,in,&fx);
    }else if(s.popup!=Popup::none){
        PopupStep(s,in,ps,w,&fx);
    }else if(page.Typing()){
        // the page's text box: typed characters and the page's own keys, nothing of the state's
        if(in.ntyped>0)page.Type(in.typed,in.ntyped<8 ? in.ntyped : 8);
        for(int k=0;k<kKeyCount;++k)
            if(k!=kToggle && Pressed(s,in,k))page.Key(s.pos,s.column,k,in);
    }else{
        // the page first: a key it takes is not down for the state this frame (prev still follows `in`)
        Input use=in;
        for(int k=0;k<kKeyCount;++k)
            if(k!=kToggle && Pressed(s,in,k) && page.Key(s.pos,s.column,k,in))use.held[k]=false;
        Clamp(s,page);
        // preset chips: keys and mouse
        for(int d=0;d<kPresetMost;++d)
            if(Pressed(s,use,kDigit1+d) && d<ps.count && CommitPage(s,page)){fx.op=Op::switchTo;fx.index=d;}
        if(Pressed(s,use,kStore) && ps.current>=0 && CommitPage(s,page)){fx.op=Op::store;fx.index=ps.current;}
        if(Pressed(s,use,kPresetMenu) && CommitPage(s,page)){
            s.popup=Popup::actions;
            s.popupCursor=ps.count>0 ? kActStore : kActNew;
            s.popupTarget=ps.current;
        }
        if(use.pointer && (Pressed(s,use,kMouseLeft) || Pressed(s,use,kMouseRight))){
            const bool left=Pressed(s,use,kMouseLeft);
            const int chip=ChipAt(ps,use.x,use.y), tab=TabAt(npages,use.x,use.y);
            if(chip>=0){
                if(CommitPage(s,page)){
                    if(chip>=ps.count){if(left)StartRename(s,ps,kActNew,-1);}
                    else if(left){fx.op=Op::switchTo;fx.index=chip;}
                    else{s.popup=Popup::actions;s.popupCursor=kActStore;s.popupTarget=chip;}
                }
            }else if(tab>=0 && left){
                if(tab!=s.tab && CommitPage(s,page)){s.tab=tab;s.column=0;s.pos=Pos{};s.top[0]=s.top[1]=s.top[2]=0;}
            }else if(left){
                if(!page.Click(s.pos,use.x,use.y))ClickColumns(s,page,use);
            }
        }
        // page tabs (stopping at both ends: no wrap from the first round to the last, 任务, which opens the map),
        // sub tabs
        const int dt=(Pressed(s,use,kTabNext) ? 1 : 0)-(Pressed(s,use,kTabPrev) ? 1 : 0);
        const int toTab=s.tab+dt<0 ? 0 : s.tab+dt>=npages ? npages-1 : s.tab+dt;
        if(dt && npages>1 && toTab!=s.tab && fx.op==Op::none && s.popup==Popup::none && CommitPage(s,page)){
            s.tab=toTab;
            s.column=0;s.pos=Pos{};s.top[0]=s.top[1]=s.top[2]=0;
        }
        Page& now=*pages[s.tab];
        const int ds=(Pressed(s,use,kSubNext) ? 1 : 0)-(Pressed(s,use,kSubPrev) ? 1 : 0);
        if(ds && now.Subs()>1 && s.popup==Popup::none){
            s.pos=Pos{Wrap(s.pos.sub+ds,now.Subs()),0,0,0};
            s.column=0;s.top[0]=s.top[1]=s.top[2]=0;
        }else if(ds && now.Subs()<=1 && s.popup==Popup::none){
            now.Cycle(s.pos,ds);
        }
        // columns
        if(s.popup==Popup::none && fx.op==Op::none){
            const int dy=(Pressed(s,use,kDown) ? 1 : 0)-(Pressed(s,use,kUp) ? 1 : 0);
            if(dy)MoveIn(s,now,dy);
            if(use.wheel){
                if(s.column==2)now.Step(s.pos,use.wheel>0 ? 1 : -1);
                else MoveIn(s,now,-use.wheel);
            }
            if(s.column==2){
                if(Pressed(s,use,kLeft))now.Step(s.pos,-1);
                if(Pressed(s,use,kRight))now.Step(s.pos,1);
                if(Pressed(s,use,kConfirm))now.Activate(s.pos);
                if(Pressed(s,use,kCancel))s.column=1;
            }else{
                const bool deeper=Count(now,s.pos,s.column+1)>0;
                if((Pressed(s,use,kRight) || Pressed(s,use,kConfirm)) && deeper)++s.column;
                else if(Pressed(s,use,kConfirm) && s.column==1)now.Activate(s.pos);   // an item that acts itself
                if(Pressed(s,use,kLeft) && s.column>0)--s.column;
                if(Pressed(s,use,kCancel)){
                    if(s.column>0)--s.column;
                    else AskClose(s,now,&fx,true);
                }
            }
        }
    }
    for(int k=0;k<kKeyCount;++k)s.prev[k]=in.held[k];
    if(s.open){
        Page& now=*pages[s.tab];
        now.Relocate(&s.pos,s.column);
        wchar_t note[kTextMost]{};
        if(now.TakeNote(note,kTextMost) && note[0])Note(s,note);
        Clamp(s,now);
    }
    return fx;
}

// A column line's label (left) and value (right): both kRowInset in from the line's ends; the value whole first (at most
// all but kRowLabelLeast of the room, the label still shows something), the label in what it leaves (Text::yieldTo,
// kRowGap before the value). With a colour swatch (ChipX, 34 wide) the label stops before it and the value starts
// after it.
inline constexpr float kRowInset=22,kRowGap=16,kRowLabelLeast=0.3f,kChipW=34;
constexpr float ChipX(const skin::Rect& r) noexcept { return r.x0+r.W()*0.5f-40; }
struct RowWidths { float label=0,value=0; };
constexpr RowWidths RowWidthsOf(const skin::Rect& r,bool value,bool chip) noexcept {
    const float room=r.W()-2*kRowInset;
    if(!value)return RowWidths{chip ? ChipX(r)-2-kRowGap-(r.x0+kRowInset) : room,0};
    if(chip)return RowWidths{ChipX(r)-2-kRowGap-(r.x0+kRowInset),r.x1-kRowInset-(ChipX(r)+kChipW+kRowGap)};
    return RowWidths{room,room*(1-kRowLabelLeast)-kRowGap};
}
// The hint bar: its texts kHintInset in from the bar's ends; the note up to kHintNoteShare of the room; the keys
// kHintGap before it, on two kHintRowSize rows (their tops kHintRowTop below the bar's) when one row does not fit.
inline constexpr float kHintInset=60,kHintGap=40,kHintNoteShare=0.45f,kHintRowSize=19;
inline constexpr float kHintRowTop[2]={2,26};

// The frame's picture of the screen (the UI thread builds it, the render thread paints it: src/armory_draw.cpp).
inline void Build(const State& s,Page* const* pages,int npages,const Presets& ps,const Words& w,View* out,
                  const StatusLines* status=nullptr) noexcept {
    using namespace detail;
    namespace L=skin::layout;
    out->Clear();
    if(!s.open || npages<=0)return;
    out->open=true;
    Layer& base=out->layer[0];
    const Page& page=*pages[Clamp(s.tab,0,npages-1)];
    skin::Rect hole{};
    const bool holed=page.Hole(s.pos,&hole);
    if(holed)skin::DiamondsAround(&base.quads,skin::Rect{0,0,skin::kWidth,skin::kHeight},hole);
    else skin::Diamonds(&base.quads,skin::Rect{0,0,skin::kWidth,skin::kHeight});
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
    for(int c=0;c<3;++c){
        if(holed)skin::PanelAround(&base.quads,L::Column(c),hole);
        else skin::Panel(&base.quads,L::Column(c));
    }
    if(FirstListRow(page)>0)
        for(int i=0;i<page.Subs();++i){
            const skin::Rect r=SubTab(page,i);
            skin::Tab(&base.quads,r,i==s.pos.sub);
            base.Say(page.SubName(i),(r.x0+r.x1)/2,r.y0+12,24,skin::TextOn(i==s.pos.sub),Align::centre,false,r.W()-12);
        }
    // what is painted around the page's hole: everything so far (the background and the panels are already cut round
    // it above; the bars, the tabs' marks and the sub tabs are clipped to it by the painter, src/armory_draw.cpp)
    if(holed && hole.W()>0 && hole.H()>0){base.hole=hole;base.holeQuads=base.quads.n;}
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
            const RowWidths rw=RowWidthsOf(r,line.value[0]!=0,line.chip!=0);
            const int labelAt=base.Say(line.label,r.x0+kRowInset,r.y0+12,26,col,Align::left,beige,rw.label);
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
                const int valueAt=base.Say(arrows ? v2 : line.value,r.x1-kRowInset,r.y0+12,26,col,Align::right,false,rw.value);
                base.Yield(labelAt,valueAt,kRowGap);   // the value whole first; the label in what it leaves
            }
            if(line.chip){   // the colour swatch left of the value, edged so a dark colour still shows
                const float x=ChipX(r), y=r.y0+11;
                base.quads.Box(skin::Rect{x-2,y-2,x+32,y+32},skin::kCellEdge);
                base.quads.Box(skin::Rect{x,y,x+30,y+30},line.chip);
            }
            if(at && c==s.column && s.popup==Popup::none)skin::Cursor(&base.quads,r);
        }
        page.Draw(s.pos,c,&base);
    }
    // hint bar, note
    if(s.popup==Popup::none) {
        // the keys left, the note right: the note whole first (up to kHintNoteShare of the bar), the keys in what it
        // leaves, on two rows when one does not fit
        const wchar_t* own=page.Hint(s.pos,s.column);
        const float span=L::kHint.W()-2*kHintInset, ty=L::kHint.y0+12;
        int noteAt=-1;
        if(s.note[0])noteAt=base.Say(s.note,L::kHint.x1-kHintInset,ty,24,0xFF8A1E0Au,Align::right,true,span*kHintNoteShare);
        else if(s.column==0)noteAt=base.Say(w.rootExit,L::kHint.x1-kHintInset,ty,24,skin::kTextDark,Align::right,true,span*kHintNoteShare);
        else if(page.Dirty())noteAt=base.Say(w.unsaved,L::kHint.x1-kHintInset,ty,24,0xFF8A1E0Au,Align::right,true,span*kHintNoteShare);
        const int keysAt=base.Say(own ? own : w.hints[s.column],L::kHint.x0+kHintInset,ty,24,skin::kTextDark,Align::left,false,span);
        base.TwoRows(keysAt,kHintRowSize,L::kHint.y0+kHintRowTop[0],L::kHint.y0+kHintRowTop[1]);
        base.Yield(keysAt,noteAt,kHintGap);
    }
    // status strip: under the hint bar, the lines side by side
    if(status && status->n>0){
        const float x0=L::kHint.x0+40, width=(L::kHint.x1-40-x0)/static_cast<float>(status->n);
        for(int i=0;i<status->n;++i)
            base.Say(status->text[i],x0+width*static_cast<float>(i),L::kHint.y1+8,20,status->warn[i] ? kStatusWarn : skin::kText,
                     Align::left,status->warn[i],width-24);
    }
    // popup
    if(s.popup==Popup::none)return;
    Layer& top=out->layer[1];
    top.quads.Box(skin::Rect{0,0,skin::kWidth,skin::kHeight},0x90000000u);
    const skin::Rect box=PopupBox();
    skin::Panel(&top.quads,box);
    const wchar_t* actions=s.popup==Popup::rename ? w.hints[4] : s.popup==Popup::confirmDelete ? w.deleteHint : w.hints[3];
    top.Say(actions,(box.x0+box.x1)/2,box.y1-34,20,skin::kText,Align::centre,false,box.W()-40);
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

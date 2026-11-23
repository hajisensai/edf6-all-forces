// 整备's 支援编成 (the squads) and 载具 (the vehicles, the mech among them, and the crews' colours) pages
// (docs/plan-2026-11-24-ui-rewrite.md stage 3) on the armory framework (armory_logic.h). Pure: no game, no files; the
// runtime (src/armory.cpp) gives the menu loaded from the ini (room_menu.h), the host's rules and a saver.
//  - Editing goes through room_menu.h's editors only (detail::Change / AddSoldier / AddPylon / ...: every value one
//    the plugin's parser takes back), on a copy of the menu both pages share; each page saves its own units through
//    room_ui_logic.h SaveItems (the same lines the old menu wrote) and the saver (loadout_store.h SaveSupportLoadouts).
//  - The centre column's top is the 3D viewport (preview_camera.h rect, SetViewPlacement): uncovered (Page::Hole) only
//    when the selection has a model the menu renderer can show (a soldier, a crew soldier, a vehicle's unified file:
//    room_preview_logic.h VehicleFileOf, its loadout's variant when it has one); otherwise (a loadout not generated
//    yet, the preview off) an opaque box that says why. The view shows ONE model
//    (docs/plan-2026-11-24-ui-rewrite.md stage 4b): the squad's selected soldier, marked so on screen.
//  - Online (the room screen) the details column's top is the "我的呼叫 / 房主规则" panel: the host's SupportDisabled
//    and the call's pt / price, read-only (a guest: the host's apply).
#pragma once
#include "armory_logic.h"
#include "armory_skin.h"
#include "hudtext.h"
#include "menu_callouts.h"
#include "preview_camera.h"
#include "room_dialog_logic.h"
#include "room_menu.h"
#include "room_preview.h"
#include "room_preview_logic.h"
#include "room_ui_logic.h"
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace crew::supportpages {
namespace ar=crew::armory;
namespace rm=crew::roommenu;
namespace rp=crew::roompreview;
namespace skin=crew::armory::skin;
using hudtext::Tx;

// ---- layout (design units, armory_skin.h layout) ---------------------------------------------------------------------
inline constexpr skin::Rect kView{638,130,1272,470};          // the 3D viewport in the centre column
inline constexpr float kTabsY0=478,kTabsY1=526;                // the squad tabs under it
inline constexpr float kTabW=150,kTabGap=8;
inline constexpr float kViewTop=400;                           // ColumnTop(1): 7 rows reserved (rows from y 536)
inline constexpr skin::Rect kPanel{1328,130,1792,356};         // the online panel in the details column
inline constexpr float kPanelTop=232;                          // ColumnTop(2): 4 rows reserved
inline constexpr float kCamW=86,kCamH=34;                      // the view buttons, bottom right inside the viewport
inline constexpr int kSquadTabs=3;                             // 12 seats at most: 第1/2/3小队
inline constexpr std::uint32_t kRing=skin::kCursor;            // the selected soldier's floor ring (orange)
inline constexpr std::uint32_t kRingOther=0x80C8C8C8u;         // the squad's other soldiers' rings (dim)
inline constexpr std::uint32_t kNoteColour=0xFFF0B060u;

inline rp::ViewRect ViewRectOf() noexcept { return rp::ViewRect{kView.x0,kView.y0,kView.W(),kView.H()}; }
inline skin::Rect SquadTab(int k) noexcept {
    const float x=kView.x0+static_cast<float>(k)*(kTabW+kTabGap);
    return skin::Rect{x,kTabsY0,x+kTabW,kTabsY1};
}
inline skin::Rect CamButton(int k) noexcept {
    const float x1=kView.x1-8-static_cast<float>(rp::kCameraPresetCount-1-k)*(kCamW+6);
    return skin::Rect{x1-kCamW,kView.y1-8-kCamH,x1,kView.y1-8};
}
// The callouts in the viewport (docs/room-3d-preview-re.md section 6.3): a mech's six (its four slots, frame, core), a
// jet's pylons. On the model (the preview drawn and its callouts file read: CalloutSpots) each box sits in the column on
// its point's side, as near the point's height as the others allow, a leader line to the point; else a mech's stand
// in their schematic places (its left on the left) and a jet's pylons are listed.
inline constexpr int kMechCallouts=6;
inline constexpr int kCalloutRow[kMechCallouts]={rm::kMechRowWeapon+3,rm::kMechRowWeapon+2,rm::kMechRowWeapon+1,
                                                 rm::kMechRowWeapon,rm::kMechRowFrame,rm::kMechRowCore};
inline constexpr float kCalloutW=220,kCalloutH=36,kPylonCalloutW=200,kPylonCalloutH=30,kCalloutGap=6,kCalloutInset=10;
inline skin::Rect SchematicCallout(int k) noexcept {   // 左肩 右肩 / 左臂 右臂 / 机体 核心
    const float x=k%2==0 ? kView.x0+kCalloutInset : kView.x1-kCalloutInset-kCalloutW;
    const float y=kView.y0+14+static_cast<float>(k/2)*(kCalloutH+60);
    return skin::Rect{x,y,x+kCalloutW,y+kCalloutH};
}
// The callout tag (menu_callouts.h) of mech row `row`.
inline int CalloutTagOf(int row) noexcept {
    namespace mc=crew::menucallouts;
    return row==rm::kMechRowFrame ? mc::kFrame : row==rm::kMechRowCore ? mc::kCore : mc::kWeapon0+(row-rm::kMechRowWeapon);
}

// ---- what the runtime gives ------------------------------------------------------------------------------------------
// The host's rules for a unit (support_call.h / support_config.h; listed false: not in the call catalog, OWN_*).
struct HostRule { bool listed=false,enabled=true; std::uint32_t pt=0; std::int32_t price=0; };
inline constexpr int kKeyNameMost=32,kFirstOwnKey=3,kOwnKeysLast=6;   // stores.h kFireLastKey
struct Env {
    bool online=false;     // the room screen (online): the rules panel shows
    bool host=true;        // this machine hosts the room (online_authority.h OnlineHostOnly)
    bool previewOn=false;  // the 3D preview's hooks are in (room_preview.h InstallPreviewHooks)
    // Whether Mods\<file> is there (room_preview.h PreviewFileThere): a vehicle's loadout just edited has no file until
    // it is saved and the generator made it. Null: every file is.
    bool (*fileThere)(const wchar_t* modsFile) noexcept=nullptr;
    // The matrix the view drew the previewed vehicle with (room_preview.h PreviewPlacement) and a unified file's callout
    // points (PreviewCallouts). Null, or false: the callouts stay schematic.
    bool (*placed)(rp::Matrix4* matrix,float* aspect) noexcept=nullptr;
    bool (*callouts)(const wchar_t* modsFile,crew::menucallouts::Callouts* out) noexcept=nullptr;
    roomui::CrewKinds kinds{};
    HostRule rule[rm::kMostUnits]{};   // by the menu's unit index
    // The fire keys a pylon's store is on (docs/multi-weapon-keys.md): the runtime fills them from the stores' own
    // StoreSpec::fireKey (stores.inc) and this machine's bindings. On the support tank (payload.cpp ControlOfSpec, a
    // tank class): 3..kOwnKeysLast its own VehicleFire<n>Key, anything else (0; the second trigger, read on the V603
    // alone) the main gun's cycle. A jet's stores (jet_combat.cpp: no StoreSpec::fireKey there) always the main weapon's
    // cycle. By LoadStore code.
    unsigned char storeKey[kLoadStoreCount+1]{};
    wchar_t switchKey[kKeyNameMost]{};                 // the main weapon's cycle: PlayerJetSwitchKey / LB, named
    wchar_t ownKey[kOwnKeysLast+1][kKeyNameMost]{};    // VehicleFire<n>Key / -Button, named, by n
};
// Writes `n` lines (all or none); false: nothing written, `why` says why.
using Saver=bool(*)(void* context,const roomui::SaveItem* items,int n,wchar_t* why,std::size_t capacity) noexcept;

// The pages' shared state (big: static, never on the stack).
struct Model {
    rm::Menu menu;
    Env env;
    Saver saver=nullptr;
    void* saverContext=nullptr;
    rm::SaveSet set;
    roomui::SaveItem items[2*rm::kMostUnits]{};
    roomui::SaveItem mine[2*rm::kMostUnits]{};
};
// Takes `units` (room_ui's menu as loaded) as the working copy.
inline void Bind(Model& m,const rm::Menu* units) noexcept {
    m.menu=units ? *units : rm::Menu{};
    m.menu.level=rm::Level::units;m.menu.rowAt=m.menu.fieldAt=0;
}
// Reads every unit again from the ini (after a preset switched the support keys): edits are dropped.
inline void Reload(Model& m,rm::IniRead read,void* context) noexcept {
    for(int i=0;i<m.menu.unitCount;++i)rm::detail::LoadUnit(m.menu.unit[i],read,context);
}

namespace detail {
inline void Put(wchar_t* out,const wchar_t* fmt,...) noexcept {
    va_list a;
    va_start(a,fmt);
    _vsnwprintf_s(out,ar::kTextMost,_TRUNCATE,fmt,a);
    va_end(a);
}
inline void Say(ar::Line* out,Tx label) noexcept { ar::CopyText(out->label,ar::kTextMost,hudtext::Tr(label)); }
// A thin frame (the viewport's edge over the 3D: nothing inside).
inline void Frame(skin::Quads* q,const skin::Rect& r,std::uint32_t c,float w=2) noexcept {
    q->Box(skin::Rect{r.x0,r.y0,r.x1,r.y0+w},c);
    q->Box(skin::Rect{r.x0,r.y1-w,r.x1,r.y1},c);
    q->Box(skin::Rect{r.x0,r.y0,r.x0+w,r.y1},c);
    q->Box(skin::Rect{r.x1-w,r.y0,r.x1,r.y1},c);
}
// A flat ring (an ellipse of `segments` quads) centred on (cx, cy).
inline void Ring(skin::Quads* q,float cx,float cy,float rx,float ry,float width,std::uint32_t c,int segments=24) noexcept {
    constexpr float kTau=6.2831853f;
    for(int i=0;i<segments;++i){
        const float a=kTau*static_cast<float>(i)/static_cast<float>(segments),b=kTau*static_cast<float>(i+1)/static_cast<float>(segments);
        const float ca=std::cos(a),sa=std::sin(a),cb=std::cos(b),sb=std::sin(b);
        q->Add(cx+ca*rx,cy+sa*ry,cx+cb*rx,cy+sb*ry,cx+ca*(rx-width),cy+sa*(ry-width*0.3f),
               cx+cb*(rx-width),cy+sb*(ry-width*0.3f),c,c,c,c);
    }
}
// A line `width` wide from (x0, y0) to (x1, y1): a callout's leader.
inline void Leader(skin::Quads* q,float x0,float y0,float x1,float y1,float width,std::uint32_t c) noexcept {
    const float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
    if(len<0.5f)return;
    const float nx=-dy/len*width*0.5f,ny=dx/len*width*0.5f;
    q->Add(x0+nx,y0+ny,x1+nx,y1+ny,x0-nx,y0-ny,x1-nx,y1-ny,c,c,c,c);
}
}  // namespace detail

// Where the model's feet land on screen (the ring's centre): its origin through Place's matrix (yaw does not move it).
inline void FeetOnScreen(const rp::ViewCamera& cam,float aspect,float out[2]) noexcept {
    const rp::Matrix4 m=rp::Place(ViewRectOf(),cam,rp::ModelFrame{},0.f,aspect);
    const float origin[3]={0,0,0};
    rp::ScreenOf(m,origin,aspect,out);
}

// ---- the pages' common part ------------------------------------------------------------------------------------------
class UnitsPage : public ar::Page {
public:
    void Bind(Model* m) noexcept {
        model=m;n=0;
        if(!m)return;
        for(int i=0;i<m->menu.unitCount && n<rm::kMostUnits;++i)if(Mine(m->menu.unit[i]))index[n++]=i;
    }
    int Rows(int) const noexcept override { return n; }
    void RowLine(int,int row,ar::Line* out) const noexcept override {
        const rm::Unit* u=UnitAt(row);
        if(!u)return;
        roomdialog::UnitLine(*u,out->label,ar::kTextMost);
        const HostRule& r=Rule(row);
        if(model->env.online && r.listed && !r.enabled)out->dim=true;   // the host refuses this call
    }
    bool Dirty() const noexcept override {
        for(int i=0;i<n;++i)if(rm::UnitChanged(model->menu.unit[index[i]]))return true;
        return false;
    }
    void Discard() noexcept override {
        for(int i=0;i<n;++i){
            rm::Unit& u=model->menu.unit[index[i]];
            u.squad=u.squadLoaded;u.vehicle=u.vehicleLoaded;u.crew=u.crewLoaded;
        }
    }
    // This page's changed units written (the other page's edits stay as they are).
    bool Commit(wchar_t* why,std::size_t cap) noexcept override {
        if(!model || !Dirty())return true;
        wchar_t own[160]{};
        if(!why || !cap){why=own;cap=sizeof(own)/sizeof(own[0]);}
        why[0]=0;
        const int all=rm::CollectSave(model->menu,&model->set) ?
            roomui::SaveItems(model->menu,model->set,model->items,2*rm::kMostUnits) : -1;
        if(all<0){ar::CopyText(why,cap,hudtext::Tr(Tx::roomSaveRefused));return false;}
        int k=0;
        for(int i=0;i<all;++i)if(Owns(model->items[i].unitIndex))model->mine[k++]=model->items[i];
        if(k==0)return true;
        if(!model->saver){ar::CopyText(why,cap,hudtext::Tr(Tx::roomSaveRefused));return false;}
        wchar_t said[ar::kTextMost]{};
        if(!model->saver(model->saverContext,model->mine,k,said,ar::kTextMost)){
            _snwprintf_s(why,cap,_TRUNCATE,hudtext::Tr(Tx::roomSaveFailed),said);
            return false;
        }
        for(int i=0;i<k;++i)roomui::MarkItemSaved(model->menu,model->mine[i]);
        return true;
    }
    float ColumnTop(int column) const noexcept override {
        return column==1 ? kViewTop : column==2 && model && model->env.online ? kPanelTop : 0;
    }
    bool Hole(const ar::Pos& p,skin::Rect* out) const noexcept override {
        if(!model || !model->env.previewOn || !PreviewOf(p,nullptr))return false;
        if(out)*out=kView;
        return true;
    }
    bool Cycle(const ar::Pos&,int delta) noexcept override {
        camera=ar::detail::Wrap(camera+delta,rp::kCameraPresetCount);
        return true;
    }
    bool Click(ar::Pos& p,float x,float y) noexcept override {
        for(int k=0;k<rp::kCameraPresetCount;++k)
            if(CamButton(k).Contains(x,y)){camera=k;return true;}
        return ClickView(p,x,y);
    }
    // What the 3D view shows at `p` (false: nothing: the viewport is an opaque box).
    virtual bool PreviewOf(const ar::Pos& p,rp::PreviewSpec* out) const noexcept=0;
    // The soldiers shown side by side at `p` (room_preview.h ShowSquad), `out` (null: only counted) the first
    // rp::kSquadMost; soldier 0 is PreviewOf's of the first of them. Below 2: one model (PreviewOf's).
    virtual int SquadOf(const ar::Pos&,rp::PreviewSpec* /*out*/) const noexcept { return 0; }
    rp::ViewCamera Camera() const noexcept { return rp::kCameraPresets[camera]; }
    int CameraIndex() const noexcept { return camera; }
    // The menu unit index of row `row` (-1 none).
    int UnitIndex(int row) const noexcept { return model && row>=0 && row<n ? index[row] : -1; }
    void Draw(const ar::Pos& p,int column,ar::Layer* out) const noexcept override {
        if(column!=0 || !model)return;   // once, whatever column has the focus
        DrawView(p,out);
        if(model->env.online)DrawPanel(p,out);
    }
protected:
    ~UnitsPage()=default;
    virtual bool Mine(const rm::Unit&) const noexcept=0;
    virtual bool ClickView(ar::Pos&,float,float) noexcept { return false; }
    // Over / in the viewport: the page's own marks (after the frame and the view buttons).
    virtual void DrawInView(const ar::Pos&,bool /*shown*/,ar::Layer*) const noexcept {}
    // Why nothing is shown (the box's caption) when the preview is on but this selection has no model.
    virtual Tx NoModelWhy(const ar::Pos&) const noexcept { return Tx::spOnlySelected; }
    bool Owns(int unitIndex) const noexcept {
        for(int i=0;i<n;++i)if(index[i]==unitIndex)return true;
        return false;
    }
    rm::Unit* UnitAt(int row) noexcept { return model && row>=0 && row<n ? &model->menu.unit[index[row]] : nullptr; }
    const rm::Unit* UnitAt(int row) const noexcept { return model && row>=0 && row<n ? &model->menu.unit[index[row]] : nullptr; }
    const HostRule& Rule(int row) const noexcept {
        static const HostRule kNone{};
        return model && row>=0 && row<n ? model->env.rule[index[row]] : kNone;
    }
    // The menu's cursor on unit row `row`, row `r`, field `f`: room_menu.h's editors read them.
    rm::Unit* Aim(int row,int r,int f) noexcept {
        rm::Unit* u=UnitAt(row);
        if(!u)return nullptr;
        model->menu.unitAt=index[row];model->menu.rowAt=r;model->menu.fieldAt=f;
        model->menu.level=rm::Level::fields;
        return u;
    }
    void DrawView(const ar::Pos& p,ar::Layer* out) const noexcept {
        const bool shown=model->env.previewOn && PreviewOf(p,nullptr);
        if(shown)detail::Frame(&out->quads,kView,skin::kCellEdge);   // the 3D shows through the hole
        else {
            out->quads.Box(kView,skin::kCell);
            detail::Frame(&out->quads,kView,skin::kCellEdge);
            const Tx why=!model->env.previewOn ? Tx::spPreviewOff : NoModelWhy(p);
            out->Say(hudtext::Tr(why),(kView.x0+kView.x1)/2,(kView.y0+kView.y1)/2-14,24,skin::kTextDim,ar::Align::centre,false,
                     kView.W()-40);
        }
        static constexpr Tx kCams[rp::kCameraPresetCount]={Tx::spCamFree,Tx::spCamFront,Tx::spCamSide,Tx::spCamTop,Tx::spCamClose};
        for(int k=0;k<rp::kCameraPresetCount;++k){
            const skin::Rect r=CamButton(k);
            skin::Tab(&out->quads,r,k==camera);
            out->Say(hudtext::Tr(kCams[k]),(r.x0+r.x1)/2,r.y0+6,20,shown ? skin::TextOn(k==camera) : skin::kTextDim,
                     ar::Align::centre,false,r.W()-8);
        }
        DrawInView(p,shown,out);
    }
    void DrawPanel(const ar::Pos& p,ar::Layer* out) const noexcept {
        skin::Cell(&out->quads,kPanel);
        const float x=kPanel.x0+16,w=kPanel.W()-32;
        float y=kPanel.y0+10;
        out->Say(hudtext::Tr(Tx::spPanelTitle),x,y,24,skin::kCursor,ar::Align::left,true,w);
        y+=44;
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return;
        out->Say(hudtext::Tr(rm::UnitChanged(*u) ? Tx::spMineEdited : Tx::spMineSaved),x,y,22,skin::kText,ar::Align::left,false,w);
        y+=40;
        const HostRule& r=Rule(p.row);
        wchar_t line[ar::kTextMost];
        if(!r.listed)ar::CopyText(line,ar::kTextMost,hudtext::Tr(Tx::spRuleUnlisted));
        else {
            wchar_t pt[ar::kTextMost];
            detail::Put(pt,hudtext::Tr(Tx::spRulePt),r.pt,r.price);
            detail::Put(line,L"%ls  %ls",hudtext::Tr(r.enabled ? Tx::spRuleOn : Tx::spRuleOff),pt);
        }
        out->Say(line,x,y,22,r.listed && !r.enabled ? 0xFFE05A3Cu : skin::kText,ar::Align::left,false,w);
        y+=40;
        out->Say(hudtext::Tr(model->env.host ? Tx::spHostRules : Tx::spGuestRules),x,y,22,skin::kTextDim,ar::Align::left,false,w);
    }
    Model* model=nullptr;
    int index[rm::kMostUnits]{};
    int n=0;
    int camera=0;
};

// ---- 支援编成: the squads --------------------------------------------------------------------------------------------
// Items: the soldiers (or the unit's own load when none), ＋添加士兵, 恢复默认. A soldier's fields: kind, colours (with
// their chips), backpack (the classes that carry one), then 复制 / 移除.
class SquadPage final : public UnitsPage {
public:
    const wchar_t* Name() const noexcept override { return hudtext::Tr(Tx::armoryTabSquad); }
    int Items(const ar::Pos& p) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        return u ? Soldiers(*u)+2 : 0;
    }
    void ItemLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return;
        const int s=Soldiers(*u);
        if(p.item<s && u->squad.count>0){
            roomdialog::SoldierLine(*u,p.item,out->label,ar::kTextMost);
            out->chip=rm::Rgba(u->squad.look[p.item].primary);
            return;
        }
        if(p.item<s){detail::Say(out,Tx::roomOwnLoad);out->acts=true;return;}
        if(p.item==s){detail::Say(out,Tx::spAddSoldier);out->acts=true;out->dim=u->squad.count>=u->seats;return;}
        detail::Say(out,Tx::spReset);out->acts=true;out->dim=u->squad.count==0;
    }
    int Fields(const ar::Pos& p) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || u->squad.count<=0 || p.item<0 || p.item>=u->squad.count)return 0;
        return rm::FieldCount(*u,p.item)+2;
    }
    void FieldLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || p.item<0 || p.item>=u->squad.count)return;
        const int values=rm::FieldCount(*u,p.item);
        const SupportLook& l=u->squad.look[p.item];
        if(p.field==values){detail::Say(out,Tx::spCopy);out->acts=true;out->dim=u->squad.count>=u->seats;return;}
        if(p.field>values){detail::Say(out,Tx::spRemove);out->acts=true;return;}
        out->steps=true;
        switch(p.field){
        case 0:detail::Say(out,Tx::roomFieldKind);
            ar::CopyText(out->value,ar::kTextMost,roomdialog::KindName(static_cast<int>(u->squad.kind[p.item])));break;
        case 1:detail::Say(out,Tx::roomFieldPrimary);roomdialog::ColourName(model->menu.palette,l.primary,out->value,ar::kTextMost);
            out->chip=rm::Rgba(l.primary);break;
        case 2:detail::Say(out,Tx::roomFieldSecondary);roomdialog::ColourName(model->menu.palette,l.secondary,out->value,ar::kTextMost);
            out->chip=rm::Rgba(l.secondary);break;
        default:detail::Say(out,Tx::roomFieldPack);ar::CopyText(out->value,ar::kTextMost,roomdialog::PackName(l.pack));break;
        }
    }
    bool Step(const ar::Pos& p,int delta) noexcept override {
        const rm::Unit* u0=UnitAt(p.row);
        if(!u0 || p.item<0 || p.item>=u0->squad.count || p.field<0 || p.field>=rm::FieldCount(*u0,p.item))return false;
        rm::Unit* u=Aim(p.row,p.item,p.field);
        return rm::detail::Change(model->menu,*u,delta)==rm::Act::changed;
    }
    bool Activate(const ar::Pos& p) noexcept override {
        const rm::Unit* u0=UnitAt(p.row);
        if(!u0)return false;
        const int s=Soldiers(*u0),count=u0->squad.count;
        if(count>0 && p.item>=0 && p.item<count && p.field>=0){   // a soldier's 复制 / 移除
            const int values=rm::FieldCount(*u0,p.item);
            if(p.field==values)return rm::detail::AddSoldier(model->menu,*Aim(p.row,p.item,0))==rm::Act::changed;
            if(p.field>values)return rm::detail::RemoveSoldier(model->menu,*Aim(p.row,p.item,0))==rm::Act::changed;
            return false;
        }
        if(p.item<s || p.item==s)   // the own load or ＋添加士兵: one more, a copy of the last
            return rm::detail::AddSoldier(model->menu,*Aim(p.row,count>0 ? count-1 : 0,0))==rm::Act::changed;
        if(p.item==s+1 && count>0)return rm::ResetUnit(model->menu,*Aim(p.row,0,0))==rm::Act::changed;
        return false;
    }
    bool PreviewOf(const ar::Pos& p,rp::PreviewSpec* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || p.item<0 || p.item>=u->squad.count)return false;
        if(out)*out=rp::PreviewSpec{u->squad.kind[p.item],p.item%4==0,u->squad.look[p.item]};
        return true;
    }
    // The cursor's squad tab (four soldiers, or the soldiers there are), left to right; its first a leader.
    int SquadOf(const ar::Pos& p,rp::PreviewSpec* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || p.item<0 || p.item>=u->squad.count)return 0;
        const int first=p.item/rp::kSquadMost*rp::kSquadMost;
        const int left=u->squad.count-first,soldiers=left<rp::kSquadMost ? left : rp::kSquadMost;
        for(int k=0;out && k<soldiers;++k)
            out[k]=rp::PreviewSpec{u->squad.kind[first+k],(first+k)%4==0,u->squad.look[first+k]};
        return soldiers;
    }
    // The column the soldier at `p` stands in (0 alone).
    int SquadSlot(const ar::Pos& p) const noexcept { return SquadOf(p,nullptr)>=2 ? p.item%rp::kSquadMost : 0; }
    // The squad tabs (第1/2/3小队, four soldiers each, shown when there are more than four).
    int SquadTabs(const ar::Pos& p) const noexcept {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || u->squad.count<=4)return 0;
        const int t=(u->squad.count+3)/4;
        return t<kSquadTabs ? t : kSquadTabs;
    }
    // The squad tabs from the keyboard / pad (A/D, the right stick left / right: kAltPrev / kAltNext), in the soldier
    // list and the soldier's fields: the first soldier of the next / previous squad, wrapping, as a tab click.
    bool Key(ar::Pos& p,int column,int key,const ar::Input&) noexcept override {
        if(key!=ar::kAltPrev && key!=ar::kAltNext)return false;
        const int tabs=SquadTabs(p);
        if(column<1 || tabs<2)return false;
        const int at=p.item>=0 && p.item/4<tabs ? p.item/4 : 0;
        p.item=4*ar::detail::Wrap(at+(key==ar::kAltNext ? 1 : -1),tabs);
        p.field=0;
        return true;
    }
    const wchar_t* Hint(const ar::Pos& p,int column) const noexcept override {
        if(column<1 || SquadTabs(p)<2)return nullptr;
        return hudtext::Tr(column==1 ? Tx::spSquadHintCentre : Tx::spSquadHintDetails);
    }
protected:
    bool Mine(const rm::Unit& u) const noexcept override { return rm::IsSquad(u); }
    bool ClickView(ar::Pos& p,float x,float y) noexcept override {
        for(int k=0;k<SquadTabs(p);++k)
            if(SquadTab(k).Contains(x,y)){p.item=4*k;p.field=0;return true;}
        // a soldier of the squad side by side: his column picks him
        const int soldiers=model && model->env.previewOn ? SquadOf(p,nullptr) : 0;
        const int k=soldiers>=2 ? rp::SquadSlotAt(ViewRectOf(),soldiers,x,y) : -1;
        if(k<0)return false;
        p.item=p.item/rp::kSquadMost*rp::kSquadMost+k;p.field=0;
        return true;
    }
    void DrawInView(const ar::Pos& p,bool shown,ar::Layer* out) const noexcept override {
        const int tabs=SquadTabs(p);
        for(int k=0;k<tabs;++k){
            const skin::Rect r=SquadTab(k);
            const bool at=p.item/4==k;
            skin::Tab(&out->quads,r,at);
            wchar_t s[ar::kTextMost];
            detail::Put(s,hudtext::Tr(Tx::spSquadTab),k+1);
            out->Say(s,(r.x0+r.x1)/2,r.y0+10,22,skin::TextOn(at),ar::Align::centre,false,r.W()-8);
        }
        if(!shown)return;
        const int soldiers=SquadOf(p,nullptr);
        if(soldiers<2) {
            float feet[2];
            FeetOnScreen(Camera(),aspect,feet);
            detail::Ring(&out->quads,feet[0],feet[1],48,13,5,kRing);
            return;
        }
        // the squad side by side (room_preview.h ShowSquad): a ring under each, the selected one's orange; each where
        // room_preview places that soldier's model (its column of the viewport, the same camera)
        const int at=SquadSlot(p);
        for(int k=0;k<soldiers;++k){
            float feet[2];
            rp::FeetOf(rp::SquadRectOf(ViewRectOf(),soldiers,k),Camera(),aspect,feet);
            if(!kView.Contains(feet[0],feet[1]))continue;   // 特写: the feet under the viewport
            const float rx=soldiers>2 ? 34.f : 44.f;   // narrower columns, smaller rings
            detail::Ring(&out->quads,feet[0],feet[1],rx,13,5,k==at ? kRing : kRingOther);
        }
    }
public:
    float aspect=16.f/9.f;   // the screen's (room_preview.cpp reads the window's; the runtime sets it)
private:
    static int Soldiers(const rm::Unit& u) noexcept { return u.squad.count>0 ? u.squad.count : 1; }
};

// ---- 载具: the vehicles, the mech, the crews -------------------------------------------------------------------------
// The fire key pylon `i` of vehicle unit `u` fires on, as the page says it (Env::storeKey).
inline void PylonFireKey(const Env& e,const rm::Unit& u,int i,wchar_t* out,std::size_t cap) noexcept {
    const int code=i>=0 && i<u.vehicle.count ? u.vehicle.store[i] : 0;
    const int key=rm::IsTank(u) && code>=1 && code<=kLoadStoreCount ? e.storeKey[code] : 0;
    if(key>=kFirstOwnKey && key<=kOwnKeysLast)_snwprintf_s(out,cap,_TRUNCATE,hudtext::Tr(Tx::spFireOwnKey),key,e.ownKey[key]);
    else _snwprintf_s(out,cap,_TRUNCATE,hudtext::Tr(rm::IsTank(u) ? Tx::spFireWithGun : Tx::spFireWithPrimary),e.switchKey);
}
// One callout: the VehiclePage item it picks and the room_menu.h row that item edits, its box, the point its leader
// goes to (anchored: on the model, inside the viewport).
struct CalloutSpot { skin::Rect box; int item=0,row=0; bool anchored=false; float ax=0,ay=0; };
inline constexpr int kCalloutSpotsMost=kMechCallouts>crew::menucallouts::kPylonCallouts ? kMechCallouts :
    crew::menucallouts::kPylonCallouts;
// Unit `u`'s callouts (`shown`: its model drawn in the viewport): a mech's six (item = its row), a jet's pylons (item
// 1+i: after the gun row). Placed by the model (the view's matrix, Env::placed, and every point of its callouts file,
// Env::callouts): each box in the column on its point's side, stacked near the point's height (menu_callouts.h Stack;
// the right column above the view buttons). Else a mech's in their schematic places, none for a jet (its pylons
// listed). None for a tank (its rounds leave from the main gun: nothing hangs on it) or a stock vehicle. Returns the
// count written to `out` (kCalloutSpotsMost).
inline int CalloutSpots(const Env& e,const rm::Unit& u,bool shown,CalloutSpot* out) noexcept {
    namespace mc=crew::menucallouts;
    if(rm::IsCrewUnit(u) || !u.vehicle.On() || rm::IsTank(u))return 0;
    const bool mech=rm::IsMech(u);
    const int n=mech ? kMechCallouts : u.vehicle.count<mc::kPylonCallouts ? u.vehicle.count : mc::kPylonCallouts;
    int tag[kCalloutSpotsMost]{};
    for(int k=0;k<n;++k) {
        out[k]=CalloutSpot{};
        out[k].row=mech ? kCalloutRow[k] : k+1;
        out[k].item=out[k].row;
        tag[k]=mech ? CalloutTagOf(out[k].row) : mc::kPylon0+k;
    }
    rp::Matrix4 m;
    float aspect=16.f/9.f;
    static mc::Callouts c;   // off the stack; the UI thread's
    wchar_t file[96];
    const rp::PreviewSpec spec=roomui::VehiclePreview(u);
    bool on=shown && e.placed && e.callouts && roompreview::VehicleFileOf(spec.body,spec.load,false,file,96) &&
        e.placed(&m,&aspect) && e.callouts(file,&c);
    for(int k=0;on && k<n;++k) {
        float xy[2]{};
        on=c.have[tag[k]] && mc::Project(m,c.p[tag[k]],aspect,xy);
        out[k].ax=xy[0];out[k].ay=xy[1];
    }
    if(!on) {
        if(!mech)return 0;
        for(int k=0;k<n;++k){out[k].box=SchematicCallout(k);out[k].ax=out[k].ay=0;}
        return n;
    }
    const float mid=(kView.x0+kView.x1)*0.5f,w=mech ? kCalloutW : kPylonCalloutW,h=mech ? kCalloutH : kPylonCalloutH;
    for(int side=0;side<2;++side) {
        int idx[kCalloutSpotsMost]{},k2=0;
        float want[kCalloutSpotsMost]{},y0[kCalloutSpotsMost]{};
        for(int k=0;k<n;++k)if((out[k].ax<mid)==(side==0)){idx[k2]=k;want[k2]=out[k].ay;++k2;}
        const float bottom=side==0 ? kView.y1-kCalloutInset : CamButton(0).y0-kCalloutGap;
        mc::Stack(want,k2,h,kCalloutGap,kView.y0+kCalloutInset,bottom,y0);
        const float x0=side==0 ? kView.x0+kCalloutInset : kView.x1-kCalloutInset-w;
        for(int j=0;j<k2;++j) {
            CalloutSpot& s=out[idx[j]];
            s.box=skin::Rect{x0,y0[j],x0+w,y0[j]+h};
            s.anchored=kView.Contains(s.ax,s.ay);
        }
    }
    return n;
}
enum class It : std::uint8_t { gun, stock, pylon, mech, crew, addPylon, reset };
struct ItemRef { It kind=It::reset; int row=0; };   // row: the room_menu.h row it edits

class VehiclePage final : public UnitsPage {
public:
    const wchar_t* Name() const noexcept override { return hudtext::Tr(Tx::armoryTabVehicle); }
    // The items of unit `u`: a crew unit its crew row; a mech its seven rows (or the stock line); a vehicle its gun row
    // and pylons (or the stock line), ＋添加挂架; the crew's colours; 恢复默认.
    static int ItemsOf(const rm::Unit& u,ItemRef* out) noexcept {
        int k=0;
        const auto add=[&](It kind,int row) noexcept { if(out)out[k]=ItemRef{kind,row}; ++k; };
        if(!rm::IsCrewUnit(u)){
            if(!u.vehicle.On())add(It::stock,0);
            else if(rm::IsMech(u))for(int r=0;r<kMechFields;++r)add(It::mech,r);
            else {add(It::gun,0);for(int i=0;i<u.vehicle.count;++i)add(It::pylon,i+1);}
        }
        if(u.hasCrew)add(It::crew,rm::CrewRow(u));
        if(!rm::IsCrewUnit(u) && !rm::IsMech(u))add(It::addPylon,0);
        add(It::reset,0);
        return k;
    }
    static ItemRef ItemOf(const rm::Unit& u,int item) noexcept {
        ItemRef all[kMechFields+kPylonsMost+4];
        const int k=ItemsOf(u,all);
        return item>=0 && item<k ? all[item] : ItemRef{};
    }
    int Items(const ar::Pos& p) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        return u ? ItemsOf(*u,nullptr) : 0;
    }
    void ItemLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return;
        const ItemRef it=ItemOf(*u,p.item);
        switch(it.kind){
        case It::stock:detail::Say(out,rm::IsMech(*u) ? Tx::roomStockMech : Tx::roomStockVehicle);out->acts=true;break;
        case It::gun:
            detail::Say(out,rm::IsTank(*u) ? Tx::spMainGun : Tx::spGuns);
            if(rm::IsTank(*u))ar::CopyText(out->value,ar::kTextMost,u->vehicle.apGun ? L"AP" : L"HE");
            break;
        case It::pylon:{
            const int i=it.row-1;
            detail::Put(out->label,L"#%d %ls",i+1,roomdialog::StoreName(u->vehicle.store[i]));
            detail::Put(out->value,L"×%d",kRoundCounts[u->vehicle.rounds[i]&0xF]);
            break;
        }
        case It::mech:roomdialog::MechLine(u->vehicle.mech,it.row,out->label,ar::kTextMost);break;
        case It::crew:{
            wchar_t name[ar::kTextMost];
            roomdialog::CrewPrimaryName(model->menu.palette,u->crew,roomdialog::OwnDefault(*u),name,ar::kTextMost);
            detail::Put(out->label,hudtext::Tr(Tx::roomCrewRow),name);
            out->chip=u->crew.set ? rm::Rgba(u->crew.look.primary) : 0;
            break;
        }
        case It::addPylon:
            if(rm::IsTank(*u))detail::Say(out,Tx::spAddRounds);
            else detail::Put(out->label,hudtext::Tr(Tx::spAddPylon),rm::UnitMassKg(*u),rm::UnitBudgetKg(*u));
            out->acts=true;out->dim=!rm::CanAddPylon(*u);
            break;
        case It::reset:detail::Say(out,Tx::spReset);out->acts=true;out->dim=!u->vehicle.On() && !u->crew.set;break;
        }
    }
    int Fields(const ar::Pos& p) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return 0;
        switch(ItemOf(*u,p.item).kind){
        case It::gun:return rm::IsTank(*u) ? 1 : 0;
        case It::pylon:return 4;   // 挂载, 弹数, 开火键 (shown), 移除
        case It::mech:return 1;
        case It::crew:return 2;
        default:return 0;
        }
    }
    void FieldLine(const ar::Pos& p,ar::Line* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return;
        const ItemRef it=ItemOf(*u,p.item);
        switch(it.kind){
        case It::gun:
            detail::Say(out,Tx::spMainGun);out->steps=true;
            ar::CopyText(out->value,ar::kTextMost,hudtext::Tr(u->vehicle.apGun ? Tx::roomGunAp : Tx::roomGunHe));
            break;
        case It::pylon:{
            const int i=it.row-1;
            if(p.field==0){detail::Say(out,Tx::roomFieldStore);out->steps=true;
                ar::CopyText(out->value,ar::kTextMost,roomdialog::StoreName(u->vehicle.store[i]));}
            else if(p.field==1){detail::Say(out,Tx::spRounds);out->steps=true;
                detail::Put(out->value,L"%d",kRoundCounts[u->vehicle.rounds[i]&0xF]);}
            else if(p.field==2){detail::Say(out,Tx::spFireKey);PylonFireKey(model->env,*u,i,out->value,ar::kTextMost);}
            else {detail::Say(out,Tx::spRemove);out->acts=true;}
            break;
        }
        case It::mech:
            ar::CopyText(out->label,ar::kTextMost,roomdialog::MechRowName(it.row));out->steps=true;
            ar::CopyText(out->value,ar::kTextMost,roomdialog::MechValueName(it.row,rm::MechRowValue(u->vehicle.mech,it.row)));
            break;
        case It::crew:
            out->steps=true;
            if(p.field==0){
                detail::Say(out,Tx::roomFieldPrimary);
                roomdialog::CrewPrimaryName(model->menu.palette,u->crew,roomdialog::OwnDefault(*u),out->value,ar::kTextMost);
                out->chip=u->crew.set ? rm::Rgba(u->crew.look.primary) : 0;
            } else {
                detail::Say(out,Tx::roomFieldSecondary);
                const std::int32_t c=u->crew.set ? u->crew.look.secondary : kStockColour;
                roomdialog::ColourName(model->menu.palette,c,out->value,ar::kTextMost);
                out->chip=rm::Rgba(c);
            }
            break;
        default:break;
        }
    }
    // The key a pylon's store fires with (docs/multi-weapon-keys.md: a jet's stores cycle with its main weapon; a tank's
    // extra rounds ride its main gun's control, src/payload.cpp).
    bool Step(const ar::Pos& p,int delta) noexcept override {
        const rm::Unit* u0=UnitAt(p.row);
        if(!u0)return false;
        const ItemRef it=ItemOf(*u0,p.item);
        int field=0;
        switch(it.kind){
        case It::gun:if(!rm::IsTank(*u0))return false;break;
        case It::pylon:if(p.field>1)return false;field=p.field;break;
        case It::mech:break;
        case It::crew:field=p.field==1 ? 1 : 0;break;
        default:return false;
        }
        rm::Unit* u=Aim(p.row,it.row,field);
        return rm::detail::Change(model->menu,*u,delta)==rm::Act::changed;
    }
    bool Activate(const ar::Pos& p) noexcept override {
        const rm::Unit* u0=UnitAt(p.row);
        if(!u0)return false;
        const ItemRef it=ItemOf(*u0,p.item);
        rm::Unit* u=Aim(p.row,it.row,0);
        switch(it.kind){
        case It::stock:rm::detail::Materialise(*u);return u->vehicle.On();
        case It::pylon:return p.field==3 && rm::detail::RemovePylon(model->menu,*u)==rm::Act::changed;
        case It::addPylon:return rm::detail::AddPylon(model->menu,*u)==rm::Act::changed;
        case It::reset:
            if(!u->vehicle.On() && !u->crew.set)return false;
            return rm::ResetUnit(model->menu,*u)==rm::Act::changed;
        default:return false;
        }
    }
    // A crew's soldier, or the vehicle as its loadout makes it: the unified file the battle spawns (its loaded variant
    // EDF6VC_LO_*, else its base; every one carries the menu's keys, room_preview_logic.h VehicleUnified). A loadout
    // edited and not yet generated has no file: nothing shown, the box says so (NoModelWhy).
    bool PreviewOf(const ar::Pos& p,rp::PreviewSpec* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return false;
        const ItemRef it=ItemOf(*u,p.item);
        if(it.kind==It::crew){if(out)*out=roomui::CrewPreview(model->menu,*u,model->env.kinds);return true;}
        if(rm::IsCrewUnit(*u) || !roompreview::VehicleUnified(u->body) || !FileThere(*u))return false;
        if(out)*out=roomui::VehiclePreview(*u);
        return true;
    }
protected:
    bool Mine(const rm::Unit& u) const noexcept override { return !rm::IsSquad(u); }
    Tx NoModelWhy(const ar::Pos&) const noexcept override { return Tx::spNotGenerated; }
    bool FileThere(const rm::Unit& u) const noexcept {
        const rp::PreviewSpec spec=roomui::VehiclePreview(u);
        wchar_t file[96];
        if(!roompreview::VehicleFileOf(spec.body,spec.load,false,file,sizeof(file)/sizeof(file[0])))return false;
        return !model->env.fileThere || model->env.fileThere(file);
    }
    // A callout picks its item: a mech's slot row, a jet's pylon.
    bool ClickView(ar::Pos& p,float x,float y) noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u)return false;
        CalloutSpot s[kCalloutSpotsMost];
        const int spots=CalloutSpots(model->env,*u,model->env.previewOn && PreviewOf(p,nullptr),s);
        for(int k=0;k<spots;++k)
            if(s[k].box.Contains(x,y)){p.item=s[k].item;p.field=0;return true;}
        return false;
    }
    void DrawInView(const ar::Pos& p,bool shown,ar::Layer* out) const noexcept override {
        const rm::Unit* u=UnitAt(p.row);
        if(!u || rm::IsCrewUnit(*u))return;
        const ItemRef at=ItemOf(*u,p.item);
        const bool mech=rm::IsMech(*u);
        CalloutSpot s[kCalloutSpotsMost];
        const int spots=CalloutSpots(model->env,*u,shown,s);
        for(int k=0;k<spots;++k) {   // the leaders first: the boxes over them
            if(!s[k].anchored)continue;
            const bool sel=at.kind==(mech ? It::mech : It::pylon) && at.row==s[k].row;
            const std::uint32_t c=sel ? skin::kCursor : skin::kCellEdge;
            const float ex=s[k].box.x0<(kView.x0+kView.x1)*0.5f ? s[k].box.x1 : s[k].box.x0;
            detail::Leader(&out->quads,ex,(s[k].box.y0+s[k].box.y1)*0.5f,s[k].ax,s[k].ay,sel ? 2.5f : 1.5f,c);
            out->quads.Box(skin::Rect{s[k].ax-4,s[k].ay-4,s[k].ax+4,s[k].ay+4},c);
        }
        for(int k=0;k<spots;++k) {
            const skin::Rect r=s[k].box;
            const bool sel=at.kind==(mech ? It::mech : It::pylon) && at.row==s[k].row;
            out->quads.Box(r,0xC0141310u);
            detail::Frame(&out->quads,r,sel ? skin::kCursor : skin::kCellEdge,sel ? 3.f : 1.f);
            wchar_t t[ar::kTextMost];
            if(mech)detail::Put(t,L"%ls：%ls",roomdialog::MechRowName(s[k].row),
                                roomdialog::MechValueName(s[k].row,rm::MechRowValue(u->vehicle.mech,s[k].row)));
            else {
                const int i=s[k].row-1;
                detail::Put(t,L"#%d %ls ×%d",i+1,roomdialog::StoreName(u->vehicle.store[i]),
                            kRoundCounts[u->vehicle.rounds[i]&0xF]);
            }
            out->Say(t,r.x0+8,r.y0+(r.H()-20)*0.5f,mech ? 18.f : 16.f,skin::kText,ar::Align::left,false,r.W()-16);
        }
        if(mech || spots>0)return;
        if(!u->vehicle.On() || u->vehicle.count<=0)return;
        // the hardpoints listed: #, 挂载, 弹数, 开火键 (a tank's: nothing hangs on its model; a jet's while its model is
        // not drawn or its callouts are not read)
        float y=kView.y0+12;
        const float x=kView.x0+14,w=kView.W()*0.62f;
        if(shown && rm::IsTank(*u)){out->Say(hudtext::Tr(Tx::spPylonsOffModel),x,y,18,kNoteColour,ar::Align::left,false,kView.W()-28);y+=28;}
        for(int i=0;i<u->vehicle.count;++i){
            wchar_t line[ar::kTextMost],key[ar::kTextMost];
            PylonFireKey(model->env,*u,i,key,ar::kTextMost);
            detail::Put(line,L"#%d  %ls ×%d  ·  %ls",i+1,roomdialog::StoreName(u->vehicle.store[i]),
                        kRoundCounts[u->vehicle.rounds[i]&0xF],key);
            const bool sel=at.kind==It::pylon && at.row==i+1;
            out->Say(line,x,y,20,sel ? skin::kCursor : skin::kText,ar::Align::left,sel,w);
            y+=30;
        }
    }
};

}  // namespace crew::supportpages

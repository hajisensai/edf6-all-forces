// The nearest-enemy pick (GameObjectBase slot 14, 0x54C4B0) through a grid of each team's members on the ground, with
// the stock answer (target_grid.cpp: the walk's call 0x54C506 goes to Index::Query; tests/target_grid_test.cpp
// compares it with the stock walk over random teams, docs in the test).
//
// The stock pick: 0x54C4B0 fills a functor (vtable 0x17C2948, its step 0x54A3B0) and walks every hostile team's set
// (0x5E0F20: teams j with relation 2 in team order, each set's members in pointer order, those without +0x2E8 and
// +0x380 bit 7), the functor scoring each member (+0x39C none but bit 1): s = |c - self| in 3D (floats), times the
// member's weight +0x47C when the functor's +0x38 is set, divided by self+0x538 for the one self+0x548 holds (weak);
// a member replaces the best (+0x20, from FLT_MAX) only when strictly lower (the first of a tie, never a NaN). Every
// soldier walked every enemy: soldiers x enemies scores a frame.
//
// The same answer in two passes:
//  1. a copy of the functor scores members nearest first (Grid::NearFirst, each hostile team's grid) until the rings
//     left cannot beat its best: B, a score some member has;
//  2. every member that can score B or lower (its ground distance, padded by 2^-18 and 1e-15 for the floats, times its
//     weight when the pick weighs, at most B), every member kept apart (below) and the held one, sorted into the walk's
//     order (team, pointer), go through the real functor. The winner scores at most B, so it is among them, and
//     dropping members that score more than the winner changes neither the winner (the first lowest) nor the best.
// What keeps a member findable by its cell (the rings' bounds hold for it): it stands in the cell Build gave it and its
// weight is at least the team's least grid weight. A member that is not (inserted since Build, moved off its cell or
// lighter since: Inserted / Updated) is kept apart, scored by every query. Taken out (Erased): never touched again.
// Pure logic over the game's layout (no hooks): target_grid.cpp keeps it current under the manager's lock.
#pragma once
#include "edf/npc_grid.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace crew::target_grid {
// TeamManager (*(EDF+0x20B2978)): its lock +8, the rows +0x38 (0x38 bytes a team), their count +0x48.
constexpr std::size_t kMgrLock=0x08,kMgrRows=0x38,kMgrTeamCount=0x48;
// A row: its std::set head +0, size +8, the relations int[] +0x18 and their count +0x28.
constexpr std::size_t kRowBytes=0x38,kRowHead=0x00,kRowSize=0x08,kRowRelations=0x18,kRowRelationCount=0x28;
constexpr int kHostile=2;
// A set node (MSVC _Tree_node): left, parent, right, isnil, the value (GameObjectBase*).
constexpr std::size_t kNodeLeft=0x00,kNodeParent=0x08,kNodeRight=0x10,kNodeNil=0x19,kNodeValue=0x20;
// GameObjectBase: position, gone, flags (bit 7: NoTarget), weight, the held one's divisor and its weak holder.
constexpr std::size_t kObjPos=0x90,kObjGone=0x2E8,kObjFlags=0x380,kObjWeight=0x47C,kObjHeld=0x548,kHeldObject=0x518;
constexpr unsigned char kObjNoTarget=0x80;
// The pick's functor (0x54C4B0's frame): the position it scores from, its best, self, the pick, whether it weighs.
constexpr std::size_t kFnPos=0x10,kFnBest=0x20,kFnSelf=0x28,kFnResult=0x30,kFnWeighs=0x38,kFnBytes=0x40;
constexpr double kShrink=1.0-0x1p-18,kSlack=1e-15;
constexpr float kLeastWeight=1.0f/64.0f;   // lighter (or not finite): kept apart, so one light member does not widen every query
constexpr float kCell=32.0f;

template<class T> T Rd(const void* p,std::size_t off) noexcept {
    T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(v));return v;
}
using Visit=void(__fastcall*)(void* functor,void* member);
inline Visit VisitOf(const void* functor) noexcept { return Rd<Visit*>(functor,0)[1]; }
inline bool Live(const void* o) noexcept { return !Rd<unsigned char>(o,kObjGone) && !(Rd<unsigned char>(o,kObjFlags)&kObjNoTarget); }

struct Stats {
    unsigned long long queries=0,answered=0,builds=0,untracked=0,apartMoves=0,first=0,second=0,members=0;
};

class Index {
public:
    // Built for this manager and frame `key`, every set still the size it was tracked at.
    bool Fresh(const void* mgr,std::uint64_t key) const noexcept {
        if(!built || mgr!=manager || key!=frame || Rd<const unsigned char*>(mgr,kMgrRows)!=rows ||
           Rd<std::uint64_t>(mgr,kMgrTeamCount)!=teams.size())return false;
        for(std::size_t t=0;t<teams.size();++t)
            if(Rd<std::uint64_t>(rows+t*kRowBytes,kRowSize)!=teams[t].size)return false;
        return true;
    }
    bool Built() const noexcept { return built; }
    bool Covers(const void* mgr,std::uint64_t key) const noexcept { return built && mgr==manager && key==frame; }
    void Drop() noexcept { built=false; }

    // Every team's set, walked as the stock walk does (in order), into its grid. Under the manager's lock.
    void Build(const void* mgr,std::uint64_t key) {
        manager=mgr;frame=key;built=true;
        rows=Rd<const unsigned char*>(mgr,kMgrRows);
        const auto count=static_cast<std::size_t>(Rd<std::uint64_t>(mgr,kMgrTeamCount));
        entries.clear();
        teams.resize(count);
        std::size_t total=0;
        for(std::size_t t=0;t<count;++t)total+=static_cast<std::size_t>(Rd<std::uint64_t>(rows+t*kRowBytes,kRowSize));
        Rehash(total*2+64);
        for(std::size_t t=0;t<count;++t) {
            Team& team=teams[t];
            team.points.clear();team.pos.clear();team.apart.clear();
            const unsigned char* const row=rows+t*kRowBytes;
            team.size=Rd<std::uint64_t>(row,kRowSize);
            const void* const head=Rd<const void*>(row,kRowHead);
            float least=std::numeric_limits<float>::infinity();
            for(const void* n=Rd<const void*>(head,kNodeLeft);n!=head;n=Next(n)) {
                void* const o=Rd<void*>(n,kNodeValue);
                const int e=Add(o,static_cast<int>(t));
                const float w=Rd<float>(o,kObjWeight);
                if(!(w>=kLeastWeight && w<std::numeric_limits<float>::infinity())){Apart(e);continue;}
                least=(std::min)(least,w);
                entries[static_cast<std::size_t>(e)].slot=static_cast<int>(team.points.size());
                team.points.push_back(e);
                const float* const p=reinterpret_cast<const float*>(static_cast<const unsigned char*>(o)+kObjPos);
                team.pos.insert(team.pos.end(),{p[0],p[1],p[2]});
            }
            team.least=team.points.empty() ? 1.0f : least;
            team.grid.Build(static_cast<int>(team.points.size()),[&](int i){ return team.pos.data()+3*static_cast<std::size_t>(i); },kCell);
        }
    }

    // A member the game put into the row `row` (0x5E2350, under the lock): kept apart this frame.
    void Inserted(const void* row,void* o) {
        if(!built)return;
        const int t=TeamOf(row);
        if(t<0){built=false;return;}
        ++teams[static_cast<std::size_t>(t)].size;
        Apart(Add(o,t));
    }
    // A member the game took out of the row `row` (0x5E2A70, under the lock): gone, never read again.
    void Erased(const void* row,const void* o) noexcept {
        if(!built)return;
        const int t=TeamOf(row);
        if(t<0){built=false;return;}
        --teams[static_cast<std::size_t>(t)].size;
        for(int e=Head(o);e>=0;e=entries[static_cast<std::size_t>(e)].next)
            if(entries[static_cast<std::size_t>(e)].team==t){Unlink(e);return;}
        built=false;   // not one we had: the walk could see what the grid does not
    }
    // `o` after its own update: kept apart when it left its cell or got lighter than its team's grid allows.
    void Updated(const void* o) {
        if(!built)return;
        for(int e=Head(o);e>=0;e=entries[static_cast<std::size_t>(e)].next) {
            Entry& en=entries[static_cast<std::size_t>(e)];
            if(en.slot<0)continue;
            const Team& team=teams[static_cast<std::size_t>(en.team)];
            const int cell=team.grid.Where(en.slot);
            const float x=Rd<float>(o,kObjPos),z=Rd<float>(o,kObjPos+8),w=Rd<float>(o,kObjWeight);
            if((cell>=0 && team.grid.CellOf(x,z)!=cell) || !(w>=team.least && w<std::numeric_limits<float>::infinity())) {
                Apart(e);++stats.apartMoves;
            }
        }
    }

    // The stock walk of `team`'s hostile teams with `functor` (the pick's: see the top), from the index built for this
    // frame (Fresh). False, nothing visited, when it cannot stand in for the walk (the caller walks instead): the team
    // or its relations out of the rows, or the functor's position not finite.
    bool Query(const void* mgr,int team,void* functor) {
        ++stats.queries;
        const auto count=teams.size();
        if(!built || mgr!=manager || team<0 || static_cast<std::size_t>(team)>=count)return false;
        const unsigned char* const row=rows+static_cast<std::size_t>(team)*kRowBytes;
        const auto relCount=Rd<std::uint64_t>(row,kRowRelationCount);
        const int* const rel=Rd<const int*>(row,kRowRelations);
        if(relCount>count)return false;
        const float sx=Rd<float>(functor,kFnPos),sz=Rd<float>(functor,kFnPos+8);
        if(!std::isfinite(sx) || !std::isfinite(sz))return false;
        hostile.clear();
        for(std::size_t j=0;j<relCount;++j)if(rel[j]==kHostile)hostile.push_back(static_cast<int>(j));
        const bool weighs=Rd<unsigned char>(functor,kFnWeighs)!=0;
        const Visit visit=VisitOf(functor);
        ++stats.answered;

        // 1. B from a copy, nearest first.
        alignas(16) unsigned char copy[kFnBytes];
        std::memcpy(copy,functor,kFnBytes);
        bool any=false;
        const auto score=[&](int e) {
            const Entry& en=entries[static_cast<std::size_t>(e)];
            if(en.gone || !Live(en.obj))return;
            any=true;++stats.first;visit(copy,en.obj);
        };
        for(const int j:hostile) {
            const Team& t=teams[static_cast<std::size_t>(j)];
            stats.members+=t.size;
            for(const int e:t.apart)score(e);
            const double least=weighs ? t.least : 1.0;
            t.grid.NearFirst(sx,sz,t.grid.All(),
                [&](float lb){ return (lb*kShrink-kSlack)*least>static_cast<double>(Rd<float>(copy,kFnBest)); },
                [&](int i){ const int e=t.points[static_cast<std::size_t>(i)];
                            if(entries[static_cast<std::size_t>(e)].slot>=0)score(e); });
        }
        if(!any)return true;   // nobody the walk would score: the functor stays as it was
        const double best=Rd<float>(copy,kFnBest);

        // 2. Every member that can score `best` or lower, in the walk's order, through the real functor.
        picks.clear();
        const auto take=[&](int e) {
            const Entry& en=entries[static_cast<std::size_t>(e)];
            if(!en.gone && Live(en.obj))picks.push_back({static_cast<std::uint64_t>(en.team),reinterpret_cast<std::uintptr_t>(en.obj)});
        };
        for(const int j:hostile) {
            const Team& t=teams[static_cast<std::size_t>(j)];
            for(const int e:t.apart)take(e);
            if(t.points.empty())continue;
            const double least=weighs ? t.least : 1.0;
            const double reach=((best>0.0 ? best/least : 0.0)+kSlack)/kShrink;
            const auto inReach=[&](int i) {
                const int e=t.points[static_cast<std::size_t>(i)];
                const Entry& en=entries[static_cast<std::size_t>(e)];
                if(en.slot<0 || en.gone)return;
                const double dx=static_cast<double>(Rd<float>(en.obj,kObjPos))-sx,dz=static_cast<double>(Rd<float>(en.obj,kObjPos+8))-sz;
                double lb=std::sqrt(dx*dx+dz*dz)*kShrink-kSlack;
                if(weighs) {
                    const double w=Rd<float>(en.obj,kObjWeight);
                    if(!(w>=0.0 && w<1e300)){take(e);return;}
                    lb*=w;
                }
                if(!(lb>best))take(e);
            };
            if(reach<1e30) {
                const float r=static_cast<float>(reach)*1.0000005f;
                t.grid.Box(sx-r,sz-r,sx+r,sz+r,inReach);
            }
            else t.grid.Box(std::numeric_limits<float>::quiet_NaN(),0.0f,0.0f,0.0f,inReach);
        }
        // The held one (its score divided): whatever its distance. Read as the functor reads it, without the weak lock's
        // count (a superset: one more member scored changes nothing).
        if(const void* const held=Rd<const void*>(Rd<const void*>(functor,kFnSelf),kObjHeld))
            if(const void* const h=Rd<const void*>(held,kHeldObject))
                for(int e=Head(h);e>=0;e=entries[static_cast<std::size_t>(e)].next)
                    if(std::find(hostile.begin(),hostile.end(),entries[static_cast<std::size_t>(e)].team)!=hostile.end())take(e);
        std::sort(picks.begin(),picks.end());
        picks.erase(std::unique(picks.begin(),picks.end()),picks.end());
        for(const auto& p:picks){++stats.second;visit(functor,reinterpret_cast<void*>(p.second));}
        return true;
    }

    Stats stats;

private:
    struct Entry { void* obj; int team; int slot; int next; bool gone; };   // slot: its grid point, -1 apart
    struct Team {
        npc::grid::Grid grid;
        std::vector<int> points;      // grid point -> entry
        std::vector<float> pos;       // the points' positions at Build
        std::vector<int> apart;       // entries scored by every query
        float least=1.0f;             // the least weight of the grid's points
        std::uint64_t size=0;         // the set's size, as tracked
    };
    const void* manager=nullptr;
    const unsigned char* rows=nullptr;
    std::uint64_t frame=0;
    bool built=false;
    std::vector<Entry> entries;
    std::vector<Team> teams;
    std::vector<int> hostile;
    std::vector<std::pair<std::uint64_t,std::uintptr_t>> picks;
    // An open-addressed map object -> its newest entry (entries of one object chained by `next`).
    std::vector<const void*> keys;
    std::vector<int> heads;
    std::size_t used=0;

    static const void* Next(const void* n) noexcept {   // the in-order successor (the stock walk's)
        const void* r=Rd<const void*>(n,kNodeRight);
        if(!Rd<unsigned char>(r,kNodeNil)) {
            for(const void* l=Rd<const void*>(r,kNodeLeft);!Rd<unsigned char>(l,kNodeNil);l=Rd<const void*>(r,kNodeLeft))r=l;
            return r;
        }
        const void* p=Rd<const void*>(n,kNodeParent);
        while(!Rd<unsigned char>(p,kNodeNil) && n==Rd<const void*>(p,kNodeRight)){n=p;p=Rd<const void*>(p,kNodeParent);}
        return p;
    }
    int TeamOf(const void* row) const noexcept {
        const auto at=static_cast<const unsigned char*>(row);
        if(at<rows)return -1;
        const auto off=static_cast<std::size_t>(at-rows);
        if(off%kRowBytes || off/kRowBytes>=teams.size())return -1;
        return static_cast<int>(off/kRowBytes);
    }
    void Apart(int e) {
        Entry& en=entries[static_cast<std::size_t>(e)];
        en.slot=-1;
        teams[static_cast<std::size_t>(en.team)].apart.push_back(e);
    }
    std::size_t Slot(const void* o) const noexcept {
        const auto h=(reinterpret_cast<std::uintptr_t>(o)>>4)*0x9E3779B97F4A7C15ull;
        return static_cast<std::size_t>(h>>32)&(keys.size()-1);
    }
    void Rehash(std::size_t want) {
        std::size_t cap=64;
        while(cap<want)cap*=2;
        keys.assign(cap,nullptr);heads.assign(cap,-1);used=0;
        for(std::size_t e=0;e<entries.size();++e)if(!entries[e].gone){entries[e].next=-1;Link(static_cast<int>(e));}
    }
    int Head(const void* o) const noexcept {
        if(keys.empty())return -1;
        for(std::size_t i=Slot(o);keys[i];i=(i+1)&(keys.size()-1))if(keys[i]==o)return heads[i];
        return -1;
    }
    int Add(void* o,int team) {
        entries.push_back(Entry{o,team,-1,-1,false});
        const int e=static_cast<int>(entries.size()-1);
        if((used+1)*2>keys.size())Rehash(keys.size()*2);   // relinks every entry, the new one too
        else Link(e);
        return e;
    }
    void Link(int e) {
        const void* const o=entries[static_cast<std::size_t>(e)].obj;
        std::size_t i=Slot(o);
        for(;keys[i];i=(i+1)&(keys.size()-1))
            if(keys[i]==o){entries[static_cast<std::size_t>(e)].next=heads[i];heads[i]=e;return;}
        keys[i]=o;heads[i]=e;++used;
    }
    void Unlink(int e) {
        Entry& en=entries[static_cast<std::size_t>(e)];
        en.gone=true;
        const std::size_t mask=keys.size()-1;
        std::size_t i=Slot(en.obj);
        while(keys[i]!=en.obj)i=(i+1)&mask;
        if(heads[i]==e)heads[i]=en.next;
        else for(int p=heads[i];p>=0;p=entries[static_cast<std::size_t>(p)].next)
            if(entries[static_cast<std::size_t>(p)].next==e){entries[static_cast<std::size_t>(p)].next=en.next;break;}
        if(heads[i]>=0)return;
        // the object's last entry: its key out, the run after it shifted back
        keys[i]=nullptr;--used;
        for(std::size_t j=(i+1)&mask;keys[j];j=(j+1)&mask) {
            const std::size_t home=Slot(keys[j]);
            if(((j-home)&mask)>=((j-i)&mask)){keys[i]=keys[j];heads[i]=heads[j];keys[j]=nullptr;i=j;}
        }
    }
};
}  // namespace crew::target_grid

namespace crew {
// The hooks (target_grid.cpp), at load: false (logged) when the stock code is not as expected, nothing left patched.
bool InstallTargetGrid() noexcept;
}

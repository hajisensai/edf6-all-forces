// Bounded ground routing, independent of EDF object layouts. All moves are checked
// against floor support and body clearance by the supplied edge query.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
namespace npc::navigation {
struct Point { float x{},y{},z{}; };
inline float Horizontal(Point a,Point b) noexcept {return std::hypot(a.x-b.x,a.z-b.z);}
inline bool Finite(Point p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
struct Profile { float radius=0.65f,height=1.8f,step=0.55f,cell=2.0f; };
enum class Edge { blocked,open,pending };
enum class Result { pending,moving,arrived,blocked };
constexpr int kNodes=1536,kPath=512;
struct Node { Point at;float g{},f{};int x{},z{},parent=-1;bool closed{}; };
struct State {
    Node nodes[kNodes]{};Point path[kPath]{};
    Point origin{},goal{},progress{};Profile profile{};
    int count{},active=-1,direction{},length{},cursor{};
    std::uint64_t retryAt{},progressAt{},checkedAt{},lastAt{};
    bool initialized{},failed{},checked{};
};
inline void Begin(State& s,Point from,Point goal,Profile p,std::uint64_t ms) noexcept {
    s.count=1;s.active=-1;s.direction=0;s.length=s.cursor=0;
    s.origin=from;s.goal=goal;s.profile=p;s.failed=false;s.initialized=true;
    s.progress=from;s.progressAt=ms;s.lastAt=ms;s.checked=false;
    s.nodes[0]={from,0.0f,Horizontal(from,goal),0,0,-1,false};
}
inline void Fail(State& s,std::uint64_t ms) noexcept {s.failed=true;s.retryAt=ms+1000;s.length=0;}
inline bool Path(State& s,int end,Point goal) noexcept {
    int ids[kPath],n=0;
    for(int i=end;i>=0;i=s.nodes[i].parent) {if(n==kPath-1)return false;ids[n++]=i;}
    s.length=0;
    for(int i=n-2;i>=0;--i)s.path[s.length++]=s.nodes[ids[i]].at;
    s.path[s.length++]=goal;s.cursor=0;s.checked=false;return true;
}
// edge(from,to,projected) must check the entire segment and project its endpoint
// to connected ground. pending consumes no search state: work resumes next call.
// The caller supplies a per-call edge budget AND a shared world-query budget.
template<class Query>
Result Navigate(State& s,Point from,Point goal,float stop,std::uint64_t ms,Point& next,Query edge,
                Profile p={},int budget=4) noexcept {
    next=from;
    if(!Finite(from)||!Finite(goal)||!std::isfinite(stop)||!(p.cell>0.0f && p.cell<=8.0f)||
       !(p.step>0.0f && p.step<=1.0f)||!(p.radius>0.0f && p.radius<=10.0f)||
       !(p.height>p.step && p.height<=20.0f))return Result::blocked;
    if(Horizontal(from,goal)<=stop && std::fabs(from.y-goal.y)<=p.step)return Result::arrived;
    const bool changed=!s.initialized||Horizontal(goal,s.goal)>p.cell*8.0f||std::fabs(goal.y-s.goal.y)>p.step||
        p.radius!=s.profile.radius||p.height!=s.profile.height||p.step!=s.profile.step||p.cell!=s.profile.cell||
        ms<s.lastAt||ms-s.lastAt>2000;
    if(changed)Begin(s,from,goal,p,ms);
    s.lastAt=ms;
    if(s.failed) {if(ms<s.retryAt)return Result::blocked;Begin(s,from,goal,p,ms);}
    if(Horizontal(from,s.progress)>0.3f) {s.progress=from;s.progressAt=ms;}
    else if(s.length && ms-s.progressAt>=1500)Begin(s,from,goal,p,ms);
    if(s.length) {
        while(s.cursor<s.length && Horizontal(from,s.path[s.cursor])<0.45f &&
              std::fabs(from.y-s.path[s.cursor].y)<=p.step){++s.cursor;s.checked=false;}
        if(s.cursor==s.length){Begin(s,from,goal,p,ms);}
        else {
            // Validate the next edge repeatedly: destroyed/new obstacles invalidate
            // cached routes. No straight-line fallback on a blocked or deferred query.
            Point projected{};
            if(!s.checked || ms-s.checkedAt>=100 || Horizontal(from,s.path[s.cursor])>p.cell*2) {
                const Edge e=edge(from,s.path[s.cursor],projected);
                if(e==Edge::pending)return Result::pending;
                if(e==Edge::blocked || std::fabs(projected.y-s.path[s.cursor].y)>p.step)Begin(s,from,goal,p,ms);
                else {s.checked=true;s.checkedAt=ms;}
            }
            if(s.length){next=s.path[s.cursor];return Result::moving;}
        }
    }
    constexpr int dx[8]={1,1,0,-1,-1,-1,0,1},dz[8]={0,1,1,1,0,-1,-1,-1};
    while(budget>0) {
        if(s.active<0) {
            float best=1e30f;
            for(int i=0;i<s.count;++i)if(!s.nodes[i].closed && s.nodes[i].f<best){best=s.nodes[i].f;s.active=i;}
            if(s.active<0){Fail(s,ms);return Result::blocked;}
            s.direction=-1;
        }
        const Node current=s.nodes[s.active];
        if(s.direction<0) {
            if(Horizontal(current.at,s.goal)<=p.cell*1.5f && std::fabs(current.at.y-s.goal.y)<=p.step*2) {
                Point projected{};const Edge e=edge(current.at,s.goal,projected);
                if(e==Edge::pending)return Result::pending;
                --budget;
                if(e==Edge::open && std::fabs(projected.y-s.goal.y)<=p.step) {
                    if(!Path(s,s.active,projected)){Fail(s,ms);return Result::blocked;}
                    // First path edge is checked from the actor's CURRENT position on
                    // the next call, since it may have moved while search was pending.
                    return Result::pending;
                }
            }
            s.direction=0;
            if(!budget)break;
        }
        const int d=s.direction;
        const int x=current.x+dx[d],z=current.z+dz[d];
        Point to{s.origin.x+static_cast<float>(x)*p.cell,current.at.y,s.origin.z+static_cast<float>(z)*p.cell},at{};
        // Local search is bounded spatially as well as in node count. Long orders
        // remain explicitly blocked if no complete route fits; never invent a path.
        const Edge e=(std::abs(x)>256 || std::abs(z)>256) ? Edge::blocked : edge(current.at,to,at);
        if(e==Edge::pending)return Result::pending;
        --budget;++s.direction;
        if(e==Edge::open) {
            int found=-1;
            for(int i=0;i<s.count;++i)if(s.nodes[i].x==x && s.nodes[i].z==z && std::fabs(s.nodes[i].at.y-at.y)<0.25f){found=i;break;}
            const float g=current.g+Horizontal(current.at,at)+std::fabs(current.at.y-at.y);
            if(found<0) {
                if(s.count==kNodes){Fail(s,ms);return Result::blocked;}
                found=s.count++;s.nodes[found]={at,g,g+Horizontal(at,s.goal),x,z,s.active,false};
            } else if(g<s.nodes[found].g) {
                auto& n=s.nodes[found];n.g=g;n.f=g+Horizontal(at,s.goal);n.parent=s.active;n.closed=false;
            }
        }
        if(s.direction==8){s.nodes[s.active].closed=true;s.active=-1;}
    }
    return Result::pending;
}
// Ground/clear are real scene queries in production, deterministic geometry in
// tests. Sample each half metre at both footprint sides and the centre; sweep
// ankles/waist/head plus vertical headroom, so corners cannot be cut diagonally.
template<class Ground,class Clear>
bool WalkEdge(Point a,Point b,Point& out,Profile p,Ground ground,Clear clear) noexcept {
    const float distance=Horizontal(a,b);
    if(!Finite(a)||!Finite(b)||distance>p.cell*3.0f)return false;
    const int steps=(std::max)(1,static_cast<int>(std::ceil(distance/0.5f)));
    const float rx=distance>0.001f ? -(b.z-a.z)/distance*p.radius : p.radius;
    const float rz=distance>0.001f ? (b.x-a.x)/distance*p.radius : 0.0f;
    Point previous=a;
    for(int i=1;i<=steps;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(steps);
        Point at{a.x+(b.x-a.x)*t,previous.y,a.z+(b.z-a.z)*t};
        if(!ground(at,p.step,at.y)||std::fabs(at.y-previous.y)>p.step)return false;
        for(int side=-1;side<=1;++side) {
            const float offset=static_cast<float>(side);
            Point foot{at.x+rx*offset,at.y,at.z+rz*offset};float floor{};
            if(!ground(foot,p.step,floor)||std::fabs(floor-at.y)>p.step)return false;
            Point low{foot.x,floor+0.08f,foot.z},high{foot.x,floor+p.height,foot.z};
            if(!clear(low,high))return false;
            for(int level=0;level<3;++level) {
                const float h=level==0 ? p.step+0.08f : level==1 ? p.height*0.65f : p.height;
                Point start{previous.x+rx*offset,previous.y+h,previous.z+rz*offset};
                Point end{foot.x,at.y+h,foot.z};
                if(!clear(start,end))return false;
            }
        }
        previous=at;
    }
    out=previous;return true;
}
} // namespace npc::navigation
namespace crew {
npc::navigation::Result GroundNavigate(npc::navigation::State&,const float* from,const float* to,float stop,
    std::uint64_t ms,float* waypoint,npc::navigation::Profile profile={}) noexcept;
}

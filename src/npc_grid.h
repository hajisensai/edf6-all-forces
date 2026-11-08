// A frame's points bucketed by where they stand on the ground (x, z), for the soldiers' questions that were a scan of the
// whole side each (npcai.cpp: the target, the friends near a target or a shot's line, the threats pressing in, the
// nearest threat): built once a frame next to the frame's lists (npcai.cpp Gather), asked by every soldier.
//
// Why: stress:100 (2026-11-07, 2132 soldiers, 1569 enemies) spent 13.5 ms a frame in the target pick alone and ~9 ms
// more in the friends' and threats' scans, each soldier going over every enemy / friend: the cost grew with soldiers x
// enemies. Through the grid a soldier looks at the cells round it only.
//
// The answers are the scans' own, not approximations (tools/npc_grid_check.cpp compares them with the brute-force
// scans over thousands of random points):
//  - Box / Capsule / NearFirst visit a superset of the points that can pass the caller's own test, which the caller
//    still applies point by point with the same float expressions; the cell of a point and of a query bound come from
//    one monotonic function (CellX / CellZ), and every bound is padded for the rounding of the caller's expressions.
//  - A point whose position is not finite or is beyond kFar is "odd": kept apart and visited by every query (the scans
//    saw it too). PickTarget / NearestHoriz go back to the scan itself when there is one (a NaN's comparisons depend on
//    the scan's order), as they do for a query point, scope or reach that is not finite.
//  - Order: PickTarget / NearestHoriz rank by (score, index): a tie goes to the lower index, as the scans' first-wins
//    did. Box / Capsule give each cell's points in index order, cells in row order; a caller that needs the scan's
//    order (Evade's first 64) takes them through FirstAround (a partial selection of the lowest indices).
// No cap: the cells are sized from the points' extent and enlarged so that there are at most ~4 per point (a far
// outlier makes the cells coarser, never the arrays huge); the arrays grow with the battle and are kept.
// Pure logic, no game memory: the frame's lists it indexes are copies (positions read once, in Gather).
#pragma once
#include "npc_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace npc::grid {
constexpr float kFar=1.0e6f;   // m from the origin: past it a point is odd (no map is near that size)

class Grid {
public:
    // Over points 0..n-1, at(i) their (x, y, z); `want`: the cell's side in m (made larger when the points spread far).
    template<class At> void Build(int n,At at,float want) {
        count=n>0 ? n : 0;odd.clear();
        float x0=0.0f,x1=0.0f,z0=0.0f,z1=0.0f;bool any=false;
        for(int i=0;i<count;++i) {
            const float* p=at(i);
            if(!Regular(p[0],p[2]))continue;
            if(!any){x0=x1=p[0];z0=z1=p[2];any=true;continue;}
            x0=(std::min)(x0,p[0]);x1=(std::max)(x1,p[0]);z0=(std::min)(z0,p[2]);z1=(std::max)(z1,p[2]);
        }
        minX=x0;minZ=z0;
        cell=want>0.5f && std::isfinite(want) ? want : 16.0f;
        // At most ~4 cells a point (and a few hundred for a handful): a coarser cell when the points spread far apart.
        const double budget=(std::max)(4.0*count,256.0);
        for(;;) {
            const double cx=std::floor((static_cast<double>(x1)-x0)/cell)+1.0,cz=std::floor((static_cast<double>(z1)-z0)/cell)+1.0;
            if(cx*cz<=budget){nx=static_cast<int>(cx);nz=static_cast<int>(cz);break;}
            cell*=2.0f;
        }
        inv=1.0f/cell;
        const std::size_t cells=static_cast<std::size_t>(nx)*static_cast<std::size_t>(nz);
        start.assign(cells+1,0);
        if(where.size()<static_cast<std::size_t>(count))where.resize(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i) {
            const float* p=at(i);
            if(!Regular(p[0],p[2])){where[static_cast<std::size_t>(i)]=-1;odd.push_back(i);continue;}
            const int c=CellZ(p[2])*nx+CellX(p[0]);
            where[static_cast<std::size_t>(i)]=c;++start[static_cast<std::size_t>(c)+1];
        }
        for(std::size_t c=0;c<cells;++c)start[c+1]+=start[c];
        if(item.size()<static_cast<std::size_t>(count))item.resize(static_cast<std::size_t>(count));
        fill.assign(start.begin(),start.end()-1);
        for(int i=0;i<count;++i)   // in index order: each cell's points ascending
            if(where[static_cast<std::size_t>(i)]>=0)item[static_cast<std::size_t>(fill[static_cast<std::size_t>(where[static_cast<std::size_t>(i)])]++)]=i;
    }
    void Clear() noexcept { count=0;nx=nz=0;odd.clear();start.clear(); }
    int Points() const noexcept { return count; }
    int Odd() const noexcept { return static_cast<int>(odd.size()); }
    float Cell() const noexcept { return cell; }

    // f(i) for every point that can lie within [x0, x1] x [z0, z1] (padded for rounding), and every odd one. A bound
    // that is not a number: every point.
    template<class F> void Box(float x0,float z0,float x1,float z1,F&& f) const {
        for(int i:odd)f(i);
        if(!nx)return;
        if(std::isnan(x0) || std::isnan(x1) || std::isnan(z0) || std::isnan(z1)){Cells(0,nx-1,0,nz-1,f);return;}
        const float px=Pad(x0,x1),pz=Pad(z0,z1);
        Cells(ClampX(x0-px),ClampX(x1+px),ClampZ(z0-pz),ClampZ(z1+pz),f);
    }
    // Every point within `r` of (x, z) on either axis (the box round it).
    template<class F> void Around(const float* c,float r,F&& f) const { Box(c[0]-r,c[2]-r,c[0]+r,c[2]+r,static_cast<F&&>(f)); }
    // f(i) for every point that can lie within `margin` of segment a-b over the ground (its capsule), and every odd one.
    // Row by row: the stretch of the segment whose z can reach the row, its x range widened by the margin.
    template<class F> void Capsule(const float* a,const float* b,float margin,F&& f) const {
        if(!nx){for(int i:odd)f(i);return;}
        if(!std::isfinite(a[0]+a[2]+b[0]+b[2]+margin)) {   // not a number or infinite: the box (every point at worst)
            Box((std::min)(a[0],b[0])-margin,(std::min)(a[2],b[2])-margin,(std::max)(a[0],b[0])+margin,(std::max)(a[2],b[2])+margin,f);
            return;
        }
        for(int i:odd)f(i);
        const float m=margin+Pad((std::min)(a[0],b[0])-margin,(std::max)(a[0],b[0])+margin)+
                      Pad((std::min)(a[2],b[2])-margin,(std::max)(a[2],b[2])+margin);
        const int r0=ClampZ((std::min)(a[2],b[2])-m),r1=ClampZ((std::max)(a[2],b[2])+m);
        const double dz=static_cast<double>(b[2])-a[2],dx=static_cast<double>(b[0])-a[0];
        for(int row=r0;row<=r1;++row) {
            // The row's z band (a point in it may sit a rounding off it: m has the pad), the segment's t that reaches it.
            const double lo=static_cast<double>(minZ)+static_cast<double>(row)*cell-m,hi=static_cast<double>(minZ)+(row+1.0)*cell+m;
            double t0=0.0,t1=1.0;
            if(std::fabs(dz)>1e-9) {
                double u0=(lo-a[2])/dz,u1=(hi-a[2])/dz;
                if(u0>u1)std::swap(u0,u1);
                t0=(std::max)(t0,u0);t1=(std::min)(t1,u1);
                if(t0>t1)continue;
            } else if(a[2]<lo || a[2]>hi) continue;
            const double xa=a[0]+dx*t0,xb=a[0]+dx*t1;
            const float xl=static_cast<float>((std::min)(xa,xb)-m),xr=static_cast<float>((std::max)(xa,xb)+m);
            const int c0=ClampX(xl),c1=ClampX(xr);
            for(int c=c0;c<=c1;++c)Run(row*nx+c,f);
        }
    }
    // The cells round (x, z) nearest first, as rings of cells; within `clip` (cells, the inclusive [cx0, cx1] x [cz0,
    // cz1], already within the grid). Before each ring past the first: stop(lb) with lb a bound no point of that ring or
    // any later one is nearer over the ground than (padded for rounding); true ends the walk. f(i) for each point.
    struct Clip { int cx0,cx1,cz0,cz1; };
    Clip All() const noexcept { return Clip{0,nx-1,0,nz-1}; }
    // The cells that can hold a point within `r` of (x, z) on either axis.
    Clip Within(float x,float z,float r) const noexcept {
        const float px=Pad(x-r,x+r),pz=Pad(z-r,z+r);
        return Clip{ClampX(x-r-px),ClampX(x+r+px),ClampZ(z-r-pz),ClampZ(z+r+pz)};
    }
    template<class Stop,class F> void NearFirst(float x,float z,Clip clip,Stop&& stop,F&& f) const {
        if(!nx || clip.cx0>clip.cx1 || clip.cz0>clip.cz1)return;
        const int cx=CellXUnclamped(x),cz=CellZUnclamped(z);
        // The ring that first meets the clip, the last one that does.
        const int gap=(std::max)({clip.cx0-cx,cx-clip.cx1,clip.cz0-cz,cz-clip.cz1,0});
        const int last=(std::max)({cx-clip.cx0,clip.cx1-cx,cz-clip.cz0,clip.cz1-cz});
        const float slack=Pad(x,x)+Pad(z,z)+1e-6f*cell*static_cast<float>(nx+nz);
        for(int r=gap;r<=last;++r) {
            if(r>0) {
                const float lb=static_cast<float>(r-1)*cell-slack;
                if(stop(lb>0.0f ? lb : 0.0f))return;
            }
            const int x0=(std::max)(cx-r,clip.cx0),x1=(std::min)(cx+r,clip.cx1),z0=(std::max)(cz-r,clip.cz0),z1=(std::min)(cz+r,clip.cz1);
            if(x0>x1 || z0>z1)continue;
            for(int row=z0;row<=z1;++row) {
                if(row==cz-r || row==cz+r){for(int c=x0;c<=x1;++c)Run(row*nx+c,f);continue;}
                if(cx-r>=clip.cx0)Run(row*nx+cx-r,f);   // the ring's sides
                if(r>0 && cx+r<=clip.cx1)Run(row*nx+cx+r,f);
            }
        }
    }

    static bool Regular(float x,float z) noexcept { return std::fabs(x)<=kFar && std::fabs(z)<=kFar; }   // NaN: false

private:
    int count=0,nx=0,nz=0;
    float minX=0.0f,minZ=0.0f,cell=16.0f,inv=1.0f/16.0f;
    std::vector<int> start,item,where,fill,odd;

    // A point's column / row: one monotonic float expression (a bound's cell is never past its point's), then clamped.
    int CellXUnclamped(float x) const noexcept { return Floor((x-minX)*inv); }
    int CellZUnclamped(float z) const noexcept { return Floor((z-minZ)*inv); }
    static int Floor(float v) noexcept {
        if(!(v>-1.0e8f))return -100000000;   // -inf (NaN never reaches here)
        if(!(v<1.0e8f))return 100000000;
        return static_cast<int>(std::floor(v));
    }
    int CellX(float x) const noexcept { return std::clamp(CellXUnclamped(x),0,nx-1); }
    int CellZ(float z) const noexcept { return std::clamp(CellZUnclamped(z),0,nz-1); }
    int ClampX(float x) const noexcept { return CellX(x); }
    int ClampZ(float z) const noexcept { return CellZ(z); }
    // A pad for a bound's rounding against the callers' own float expressions (a few ulps of the magnitudes in play).
    float Pad(float a,float b) const noexcept {
        const float m=(std::max)({std::fabs(a),std::fabs(b),std::fabs(minX),std::fabs(minZ)});
        return std::isfinite(m) ? 1e-3f+m*4e-6f : 0.0f;
    }
    template<class F> void Run(int c,F& f) const {
        const int e=start[static_cast<std::size_t>(c)+1];
        for(int k=start[static_cast<std::size_t>(c)];k<e;++k)f(item[static_cast<std::size_t>(k)]);
    }
    template<class F> void Cells(int x0,int x1,int z0,int z1,F& f) const {
        for(int row=z0;row<=z1;++row)for(int c=x0;c<=x1;++c)Run(row*nx+c,f);
    }
};

// --- The scans, through the grid (each the same answer as the scan it names) ---

// npc_logic.h PickTarget through `g` (built over the same n points): the nearest first from the eye, done once no
// point left can rank before the best (its score's bound from the ring's distance: TargetScore is monotonic in the
// distance, the held target's kKeepTarget taken off every one). A tie: the lower index (the scan's first). usable is
// asked as the scan asks it, of a point that would become the best (its answers fixed within the frame: the same pick;
// it may be asked of other points than the scan asked).
template<class AimOf,class IsCurrent,class Usable>
int PickTarget(const Grid& g,int n,AimOf aimOf,IsCurrent isCurrent,Usable usable,const float* eye,const float* anchor,
               float scope,float reach) noexcept {
    if(g.Points()!=n || g.Odd() || !std::isfinite(eye[0]+eye[1]+eye[2]+anchor[0]+anchor[2]+scope+reach) || !(scope>=0.0f) ||
       !Grid::Regular(eye[0],eye[2]) || !Grid::Regular(anchor[0],anchor[2]))
        return npc::PickTarget(n,aimOf,isCurrent,usable,eye,anchor,scope,reach);
    int best=-1;float bestScore=0.0f;
    g.NearFirst(eye[0],eye[2],g.Within(anchor[0],anchor[2],scope),[&](float lb) noexcept {
        if(best<0)return false;
        const float d=lb*(1.0f-1e-5f);   // Dist's own rounding
        return d-kKeepTarget+(reach>0.0f && d>reach ? kOutOfReach : 0.0f)>bestScore;
    },[&](int i) {
        const float* aim=aimOf(i);
        if(Horiz(anchor,aim)>scope)return;
        const float score=TargetScore(Dist(eye,aim),reach,isCurrent(i));
        if(best>=0 && !(score<bestScore || (score==bestScore && i<best)))return;
        if(!usable(i))return;
        best=i;bestScore=score;
    });
    return best;
}

// The point nearest `pos` over the ground (Horiz), -1 with none (the scan: d<nd from 1e30, the first of a tie).
template<class AimOf> int NearestHoriz(const Grid& g,int n,AimOf aimOf,const float* pos) noexcept {
    const auto scan=[&]() noexcept {
        int best=-1;float nd=1e30f;
        for(int i=0;i<n;++i){const float d=Horiz(pos,aimOf(i));if(d<nd){nd=d;best=i;}}
        return best;
    };
    if(g.Points()!=n || g.Odd() || !Grid::Regular(pos[0],pos[2]))return scan();
    int best=-1;float nd=1e30f;
    g.NearFirst(pos[0],pos[2],g.All(),[&](float lb) noexcept { return best>=0 && lb*(1.0f-1e-5f)>nd; },[&](int i) {
        const float d=Horiz(pos,aimOf(i));
        if(d<nd || (d==nd && best>=0 && i<best)){nd=d;best=i;}
    });
    return best;
}

// The scan `for(i=0; i<n && got<most; ++i) if(keep(i)) out[got++]=i` through `g`: the first `most` points in index
// order for which keep(i) holds, ascending, into `out`, keep asked only of the points Around(c, r) visits (it must hold
// of none outside that box: the caller's own test of "within r"). Those it holds of are gathered in the cells' order,
// then the `most` lowest indices taken by a partial selection (nth_element, linear in what was gathered) and only they
// sorted: a dense crowd round `c` (hundreds in reach) costs O(k + most log most), not O(k log k).
template<class Keep> void FirstAround(const Grid& g,const float* c,float r,int most,Keep keep,std::vector<int>& out) {
    out.clear();
    if(most<=0)return;
    g.Around(c,r,[&](int i){ if(keep(i))out.push_back(i); });
    if(out.size()>static_cast<std::size_t>(most)) {
        std::nth_element(out.begin(),out.begin()+most,out.end());
        out.resize(static_cast<std::size_t>(most));
    }
    std::sort(out.begin(),out.end());
}

// The friends a shot from `from` to `to` can touch (ShotClear's two tests: within `line` of its line, within `blast` of
// where it lands; `line` and `blast` with the largest radius among them already added), ascending and once each, into
// `out`; ShotClear over them is ShotClear over all.
inline void ShotCandidates(const Grid& g,const float* from,const float* to,float line,float blast,std::vector<int>& out) {
    out.clear();
    g.Capsule(from,to,line,[&](int i){ out.push_back(i); });
    if(blast>0.0f)g.Around(to,blast,[&](int i){ out.push_back(i); });
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
}
}  // namespace npc::grid

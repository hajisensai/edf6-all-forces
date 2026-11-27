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
//  - A point whose position is not finite is "odd": kept apart and visited by every query (the scans saw it too).
//    PickTarget / NearestHoriz go back to the scan itself when there is one (a NaN's comparisons depend on the scan's
//    order), as they do for a query point beyond kFar or a scope or reach that is not finite.
//  - A finite point off the main body (Body: beyond kFar, or a stray far from the rest) is kept apart as well, visited
//    by every query (NearFirst too, before its rings): the pick and the nearest stay on the grid; the cells cover the
//    body only, so one far point does not make them coarse.
//  - Order: PickTarget / NearestHoriz rank by (score, index): a tie goes to the lower index, as the scans' first-wins
//    did. Box / Capsule give each cell's points in index order, cells in row order; a caller that needs the scan's
//    order (Evade's first 64) takes them through FirstAround (a partial selection of the lowest indices).
// No cap: the cells are sized from the main body's extent and enlarged so that there are at most ~4 per point (a body
// spread far makes the cells coarser, never the arrays huge); the arrays grow with the battle and are kept.
// Pure logic, no game memory: the frame's lists it indexes are copies (positions read once, in Gather).
// Shared by both plugins (common/edf/): EDF6VehicleCrew's soldiers (npcai.cpp; the picks through it that rank by npc_logic.h
// are src/npc_grid.h) and EDF6AutoTurret's scan of the world snapshot (autoturret/src/plugin.cpp ScanEnemies).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace npc::grid {
constexpr float kFar=1.0e6f;   // m from the origin: past it a point is off the main body (no map is near that size)

class Grid {
public:
    // Over points 0..n-1, at(i) their (x, y, z); `want`: the cell's side in m (made larger when the points spread far).
    template<class At> void Build(int n,At at,float want) {
        count=n>0 ? n : 0;apart.clear();oddCount=0;
        cell=want>0.5f && std::isfinite(want) ? want : 16.0f;
        // The main body's box: the points on the map (Regular), less a few far from the rest (Body).
        xs.clear();zs.clear();
        for(int i=0;i<count;++i) {
            const float* p=at(i);
            if(Regular(p[0],p[2])){xs.push_back(p[0]);zs.push_back(p[2]);}
        }
        float bx0=0.0f,bx1=0.0f,bz0=0.0f,bz1=0.0f;
        const bool body=!xs.empty();
        if(body){Body(xs,cell,bx0,bx1);Body(zs,cell,bz0,bz1);}
        const auto inBody=[&](const float* p) noexcept { return body && p[0]>=bx0 && p[0]<=bx1 && p[2]>=bz0 && p[2]<=bz1; };
        // Its points' own extent (the cells start at the lowest of them).
        float x0=0.0f,x1=0.0f,z0=0.0f,z1=0.0f;bool any=false;
        for(int i=0;i<count;++i) {
            const float* p=at(i);
            if(!inBody(p))continue;
            if(!any){x0=x1=p[0];z0=z1=p[2];any=true;continue;}
            x0=(std::min)(x0,p[0]);x1=(std::max)(x1,p[0]);z0=(std::min)(z0,p[2]);z1=(std::max)(z1,p[2]);
        }
        minX=x0;minZ=z0;
        // At most ~4 cells a point (and a few hundred for a handful): a coarser cell when the body spreads far.
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
            if(!inBody(p)) {   // not finite (odd), or off the body
                where[static_cast<std::size_t>(i)]=-1;apart.push_back(i);
                if(!std::isfinite(p[0]) || !std::isfinite(p[2]))++oddCount;
                continue;
            }
            const int c=CellZ(p[2])*nx+CellX(p[0]);
            where[static_cast<std::size_t>(i)]=c;++start[static_cast<std::size_t>(c)+1];
        }
        for(std::size_t c=0;c<cells;++c)start[c+1]+=start[c];
        int occupiedRows=0;
        nextOccupiedRow.resize(static_cast<std::size_t>(nz)+1);
        nextOccupiedRow[static_cast<std::size_t>(nz)]=nz;
        for(int row=nz-1;row>=0;--row) {
            const auto first=static_cast<std::size_t>(row)*static_cast<std::size_t>(nx);
            const auto after=first+static_cast<std::size_t>(nx);
            const bool occupied=start[first]!=start[after];
            occupiedRows+=occupied;
            nextOccupiedRow[static_cast<std::size_t>(row)]=occupied ? row : NextRow(row+1);
        }
        hasEmptyRows=occupiedRows!=nz;
        if(item.size()<static_cast<std::size_t>(count))item.resize(static_cast<std::size_t>(count));
        fill.assign(start.begin(),start.end()-1);
        for(int i=0;i<count;++i)   // in index order: each cell's points ascending
            if(where[static_cast<std::size_t>(i)]>=0)item[static_cast<std::size_t>(fill[static_cast<std::size_t>(where[static_cast<std::size_t>(i)])]++)]=i;
    }
    void Clear() noexcept { count=0;nx=nz=0;apart.clear();oddCount=0;start.clear(); }
    int Points() const noexcept { return count; }
    // Points whose position is not finite (the pick and the nearest go back to the scans over them).
    int Odd() const noexcept { return oddCount; }
    // Points kept apart from the cells, visited by every query: the odd ones and those off the main body (far ones).
    int Apart() const noexcept { return static_cast<int>(apart.size()); }
    float Cell() const noexcept { return cell; }

    // f(i) for every point that can lie within [x0, x1] x [z0, z1] (padded for rounding), and every one apart. A bound
    // that is not a number: every point.
    template<class F> void Box(float x0,float z0,float x1,float z1,F&& f) const {
        AnyInBox(x0,z0,x1,z1,[&](int i){ f(i);return false; });
    }
    // Every point within `r` of (x, z) on either axis (the box round it).
    template<class F> void Around(const float* c,float r,F&& f) const { Box(c[0]-r,c[2]-r,c[0]+r,c[2]+r,static_cast<F&&>(f)); }
    template<class Match> bool AnyAround(const float* c,float r,Match&& match) const {
        return AnyInBox(c[0]-r,c[2]-r,c[0]+r,c[2]+r,match);
    }

    // The lowest point indices accepted by keep(), in scan order. Each cell and the apart list are already ordered.
    template<class Keep> void FirstAround(const float* c,float r,int most,Keep keep,std::vector<int>& out) const {
        out.clear();
        if(most<=0)return;
        if(most<count && nx && FirstInOrder(c,r,most,keep,out))return;
        Around(c,r,[&](int i){ if(keep(i))out.push_back(i); });
        if(out.size()>static_cast<std::size_t>(most)) {
            std::nth_element(out.begin(),out.begin()+most,out.end());
            out.resize(static_cast<std::size_t>(most));
        }
        std::sort(out.begin(),out.end());
    }
    // Visit the capsule's candidate points in row order and stop as soon as one matches. Each row uses the
    // stretch of the segment whose z can reach it, widened by the margin.
    template<class Match> bool AnyInCapsule(const float* a,const float* b,float margin,Match&& match) const {
        for(int i:apart)if(match(i))return true;
        if(!nx)return false;
        if(!std::isfinite(a[0]+a[2]+b[0]+b[2]+margin))return AnyInSpan(0,nx*nz-1,match);
        const float m=margin+Pad((std::min)(a[0],b[0])-margin,(std::max)(a[0],b[0])+margin)+
                      Pad((std::min)(a[2],b[2])-margin,(std::max)(a[2],b[2])+margin);
        const int r0=ClampZ((std::min)(a[2],b[2])-m),r1=ClampZ((std::max)(a[2],b[2])+m);
        const double dz=static_cast<double>(b[2])-a[2],dx=static_cast<double>(b[0])-a[0];
        const bool variesZ=std::fabs(dz)>1e-9;
        const double invZ=variesZ ? 1.0/dz : 0.0;
        return AnyInRows(r0,r1,[&](int row) {
            // The row's z band (a point in it may sit a rounding off it: m has the pad), the segment's t that reaches it.
            const double lo=static_cast<double>(minZ)+static_cast<double>(row)*cell-m,hi=static_cast<double>(minZ)+(row+1.0)*cell+m;
            double t0=0.0,t1=1.0;
            if(variesZ) {
                double u0=(lo-a[2])*invZ,u1=(hi-a[2])*invZ;
                if(u0>u1)std::swap(u0,u1);
                t0=(std::max)(t0,u0);t1=(std::min)(t1,u1);
                if(t0>t1)return false;
            } else if(a[2]<lo || a[2]>hi) return false;
            const double xa=a[0]+dx*t0,xb=a[0]+dx*t1;
            const float xl=static_cast<float>((std::min)(xa,xb)-m),xr=static_cast<float>((std::max)(xa,xb)+m);
            const int c0=ClampX(xl),c1=ClampX(xr);
            return AnyInSpan(row*nx+c0,row*nx+c1,match);
        });
    }
    template<class F> void Capsule(const float* a,const float* b,float margin,F&& f) const {
        AnyInCapsule(a,b,margin,[&](int i){ f(i);return false; });
    }
    // The cells round (x, z) nearest first, as rings of cells; within `clip` (cells, the inclusive [cx0, cx1] x [cz0,
    // cz1], already within the grid). Before each ring past the first: stop(lb) with lb a bound no point of that ring or
    // any later one is nearer over the ground than (padded for rounding); true ends the walk. f(i) for each point; the
    // points apart first, all of them (an order-free caller: it ranks what it is given).
    struct Clip { int cx0,cx1,cz0,cz1; };
    Clip All() const noexcept { return Clip{0,nx-1,0,nz-1}; }
    // The cells that can hold a point within `r` of (x, z) on either axis.
    Clip Within(float x,float z,float r) const noexcept {
        const float px=Pad(x-r,x+r),pz=Pad(z-r,z+r);
        return Clip{ClampX(x-r-px),ClampX(x+r+px),ClampZ(z-r-pz),ClampZ(z+r+pz)};
    }
    template<class Stop,class F> void NearFirst(float x,float z,Clip clip,Stop&& stop,F&& f) const {
        for(int i:apart)f(i);   // first, whatever the clip and the bound (the caller's own test still applies)
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
            const int top=cz-r,bottom=cz+r,left=cx-r,right=cx+r;
            if(z0==top) {
                RunSpan(top*nx+x0,top*nx+x1,f);
                if(top==bottom)continue;
            }
            const bool leftInClip=left>=clip.cx0,rightInClip=right<=clip.cx1;
            if(leftInClip || rightInClip) {
                const int sideStart=(std::max)(z0,top+1),sideEnd=(std::min)(z1,bottom-1);
                for(int row=NextRow(sideStart);row<=sideEnd;row=NextRow(row+1)) {
                    if(leftInClip)RunSpan(row*nx+left,row*nx+left,f);
                    if(rightInClip)RunSpan(row*nx+right,row*nx+right,f);
                }
            }
            if(z1==bottom)RunSpan(bottom*nx+x0,bottom*nx+x1,f);
        }
    }

    static bool Regular(float x,float z) noexcept { return std::fabs(x)<=kFar && std::fabs(z)<=kFar; }   // NaN: false

private:
    int count=0,nx=0,nz=0;
    float minX=0.0f,minZ=0.0f,cell=16.0f,inv=1.0f/16.0f;
    int oddCount=0;
    std::vector<int> start,item,where,fill,apart;
    // First occupied row at or after this row, or nz. Rebuilt with the offsets.
    std::vector<int> nextOccupiedRow;
    bool hasEmptyRows=false;
    std::vector<float> xs,zs;   // Build's scratch

    // The main body of the points' values `v` (finite, within kFar) on one axis, [lo, hi]: the values between the
    // 1/64 quantiles (trimmed: a few far points do not count), widened by a quarter of that span and two cells. The
    // values past it are a few strays (at most ~1/32 of them on the axis, kept apart: visited by every query), and the
    // cells cover the body only: one far point no longer makes every cell coarse. Fewer than 16 values: all of them.
    static void Body(std::vector<float>& v,float cell,float& lo,float& hi) {
        const std::size_t m=v.size();
        if(m<16){lo=*std::min_element(v.begin(),v.end());hi=*std::max_element(v.begin(),v.end());return;}
        const std::size_t k=m/64>0 ? m/64 : 1;
        const auto first=v.begin(),last=v.end();
        const float pivot=v[m/2];
        const bool allEqual=v.front()==pivot && v.back()==pivot &&
            std::all_of(first,last,[pivot](float x){return x==pivot;});
        float q0=pivot,q1=pivot;
        if(!allEqual) {
            // Each rank only needs to search the side that contains it. Small arrays use the original selection.
            const auto split=m>=256 ? std::partition(first,last,[pivot](float x){return x<pivot;}) : first;
            const auto select=[first,split,last](auto rank) {
                const bool below=rank<split;
                std::nth_element(below ? first : split,rank,below ? split : last);
                return *rank;
            };
            q0=select(first+static_cast<std::ptrdiff_t>(k));
            q1=select(first+static_cast<std::ptrdiff_t>(m-1-k));
        }
        const float pad=(q1-q0)*0.25f+2.0f*cell;
        lo=q0-pad;hi=q1+pad;
    }

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
    int NextRow(int row) const noexcept { return nextOccupiedRow[static_cast<std::size_t>(row)]; }

    template<class Keep> bool FirstInOrder(const float* c,float r,int most,Keep& keep,std::vector<int>& out) const {
        if(std::isnan(c[0]-r) || std::isnan(c[2]-r) || std::isnan(c[0]+r) || std::isnan(c[2]+r))return false;
        const Clip clip=Within(c[0],c[2],r);
        constexpr int kMaxCells=64;
        const int width=clip.cx1-clip.cx0+1,height=clip.cz1-clip.cz0+1;
        if(width<=0 || height<=0 || width>kMaxCells/height)return false;

        struct Run { const int* next; const int* end; };
        Run runs[kMaxCells+1];
        int nr=0,candidates=0;
        const auto add=[&](const int* next,int size) {
            runs[nr++]={next,next+size};
            candidates+=size;
        };
        if(!apart.empty())add(apart.data(),static_cast<int>(apart.size()));
        for(int z=NextRow(clip.cz0);z<=clip.cz1;z=NextRow(z+1)) {
            for(int x=clip.cx0;x<=clip.cx1;++x) {
                const auto cellIndex=static_cast<std::size_t>(z*nx+x);
                const int begin=start[cellIndex],size=start[cellIndex+1]-begin;
                if(size)add(item.data()+begin,size);
            }
        }
        if(nr==0)return true;
        const auto accept=[&](int i) { if(keep(i))out.push_back(i); };
        const auto needMore=[&]() { return out.size()<static_cast<std::size_t>(most); };
        if(nr==1) {
            for(const int* i=runs[0].next;i!=runs[0].end && needMore();++i)accept(*i);
            return true;
        }
        // Sparse or wide queries are cheaper to select in one pass. Dense local queries can stop the merge early.
        if(candidates/4<most)return false;
        const auto later=[](const Run& a,const Run& b){ return *a.next>*b.next; };
        std::make_heap(runs,runs+nr,later);
        while(nr && needMore()) {
            std::pop_heap(runs,runs+nr,later);
            Run& smallest=runs[nr-1];
            accept(*smallest.next++);
            if(smallest.next==smallest.end)--nr;
            else std::push_heap(runs,runs+nr,later);
        }
        return true;
    }

    template<class MatchRow> bool AnyInRows(int first,int last,MatchRow&& match) const {
        if(hasEmptyRows) {
            for(int row=NextRow(first);row<=last;row=NextRow(row+1))
                if(match(row))return true;
        } else {
            for(int row=first;row<=last;++row)
                if(match(row))return true;
        }
        return false;
    }
    template<class F> void RunSpan(int first,int last,F& f) const {
        const int end=start[static_cast<std::size_t>(last)+1];
        for(int k=start[static_cast<std::size_t>(first)];k<end;++k)f(item[static_cast<std::size_t>(k)]);
    }
    // The items for adjacent cells are already contiguous. Two offsets cover a whole row span.
    template<class Match> bool AnyInSpan(int first,int last,Match& match) const {
        const int end=start[static_cast<std::size_t>(last)+1];
        for(int k=start[static_cast<std::size_t>(first)];k<end;++k)
            if(match(item[static_cast<std::size_t>(k)]))return true;
        return false;
    }
    template<class Match> bool AnyInBox(float x0,float z0,float x1,float z1,Match&& match) const {
        for(int i:apart)if(match(i))return true;
        if(!nx)return false;
        if(std::isnan(x0) || std::isnan(x1) || std::isnan(z0) || std::isnan(z1))return AnyInSpan(0,nx*nz-1,match);
        const float px=Pad(x0,x1),pz=Pad(z0,z1);
        const int c0=ClampX(x0-px),c1=ClampX(x1+px);
        if(c0>c1)return false;
        const int r0=ClampZ(z0-pz),r1=ClampZ(z1+pz);
        return AnyInRows(r0,r1,[&](int row){ return AnyInSpan(row*nx+c0,row*nx+c1,match); });
    }
};

}  // namespace npc::grid

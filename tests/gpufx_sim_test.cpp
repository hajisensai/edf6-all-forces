// Offline check of the GPU particle effects' pure half (src/gpufx/gpufx_sim.h): curves, the pool's budget and
// replacement, the step's physics, back-to-front ordering, the depth formulas against standard and reversed
// projections, and the procedural smoke flipbook (clear tile borders, determinism, time).
#include "gpufx/gpufx_efs.h"
#include "gpufx/gpufx_layers.h"
#include "gpufx/gpufx_sim.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace crew::gpufx;

static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } } while(0)
static bool Near(float a,float b,float e=1e-4f) { return std::fabs(a-b)<=e; }

// A D3D-style row-vector perspective looking down -Z (as the engine's Commander view+0x60), standard or reversed depth.
static void Perspective(float p[16],float yScale,float aspect,float zn,float zf,bool reversed) {
    for(int i=0;i<16;++i)p[i]=0.0f;
    p[0]=yScale/aspect;p[5]=yScale;p[11]=-1.0f;
    if(!reversed){p[10]=zf/(zn-zf);p[14]=zn*zf/(zn-zf);}
    else {p[10]=zn/(zf-zn);p[14]=zn*zf/(zf-zn);}
}

static void Curves() {
    Curve c;c.Add(0.0f,0.0f).Add(0.1f,0.8f).Add(1.0f,0.0f);
    CHECK(Near(c.Eval(-1.0f),0.0f));CHECK(Near(c.Eval(0.05f),0.4f));CHECK(Near(c.Eval(0.1f),0.8f));
    CHECK(Near(c.Eval(0.55f),0.4f));CHECK(Near(c.Eval(2.0f),0.0f));
    CHECK(Near(Curve::Constant(3.0f).Eval(0.7f),3.0f));
    CHECK(Near(Curve{}.Eval(0.5f),0.0f));
    Curve full;for(int i=0;i<kKeys+3;++i)full.Add(static_cast<float>(i),1.0f);
    CHECK(full.n==kKeys);
}

static void Budget() {
    static Pool<8> pool;
    Desc d{};d.lifeMin=d.lifeMax=10.0f;
    const int id=pool.Register(d);
    CHECK(id==0);
    Rng rng(7);
    const float at[3]{},v[3]{};
    CHECK(pool.Spawn(5,at,v,rng)==-1);   // unknown desc
    for(int i=0;i<8;++i)CHECK(pool.Spawn(id,at,v,rng)>=0);
    CHECK(pool.Live()==8 && pool.Dropped()==0);
    pool.Step(5.0f);   // all half through
    int slot=pool.Spawn(id,at,v,rng);   // full: replaces the one nearest its end
    CHECK(slot>=0 && pool.Live()==8 && pool.Dropped()==1);
    // The new particle is the youngest: the next replacement must not take it.
    const int again=pool.Spawn(id,at,v,rng);
    CHECK(again>=0 && again!=slot && pool.Dropped()==2);
    pool.Step(5.5f);   // the 6 left from the first wave die (age 10.5 > 10)
    CHECK(pool.Live()==2);
    pool.Clear();
    CHECK(pool.Live()==0 && pool.Dropped()==0);
    for(int i=0;i<kDescs+2;++i)pool.Register(d);
    CHECK(pool.Register(d)==-1);
}

static void Physics() {
    static Pool<4> pool;
    Desc d{};d.lifeMin=d.lifeMax=100.0f;d.drag=0.5f;d.buoyancy=0.0f;d.windFollow=1.0f;d.spin=0.0f;
    const int id=pool.Register(d);
    Rng rng(1);
    const float at[3]{0,0,0},v[3]{10,0,0};
    pool.Spawn(id,at,v,rng);
    pool.SetWind(0.0f,0.0f,2.0f);
    // 1 s in 60 steps: the velocity relaxes towards the wind by (1-drag)^1 = 0.5 whatever the step count.
    for(int i=0;i<60;++i)pool.Step(1.0f/60.0f);
    Instance out[4];
    const int n=pool.Build(out,4,kFlipFrames);
    CHECK(n==1);
    // x: integral of 10*0.5^t over [0,1] = 10*(1-0.5)/ln2 = 7.213 (Euler: within 0.1)
    CHECK(Near(out[0].pos[0],7.213f,0.1f));
    // z: integral of 2*(1-0.5^t) = 2*(1-0.7213) = 0.557
    CHECK(Near(out[0].pos[2],0.557f,0.05f));
    // buoyancy: v_y += b*dt
    static Pool<2> rise;
    Desc b{};b.lifeMin=b.lifeMax=100.0f;b.drag=0.0f;b.buoyancy=2.0f;b.windFollow=0.0f;
    const int bid=rise.Register(b);
    const float zero[3]{};
    rise.Spawn(bid,zero,zero,rng);
    for(int i=0;i<100;++i)rise.Step(0.01f);
    rise.Build(out,2,1);
    CHECK(Near(out[0].pos[1],1.0f,0.03f));   // 0.5*2*1^2
    CHECK(Near(out[0].frame,0.0f));            // one frame: always 0
    // Life curves: size and alpha along the life fraction; the whole flipbook over the life when frameRate is 0.
    static Pool<2> life;
    Desc l{};l.lifeMin=l.lifeMax=2.0f;l.sizeMin=l.sizeMax=1.0f;l.size=Curve{};l.size.Add(0,1).Add(1,3);
    l.alpha=Curve{};l.alpha.Add(0,1).Add(1,0);l.frameRate=0.0f;
    const int lid=life.Register(l);
    life.Spawn(lid,zero,zero,rng);
    life.Step(1.0f);
    life.Build(out,2,kFlipFrames);
    CHECK(Near(out[0].size,2.0f));CHECK(Near(out[0].color[3],0.5f));CHECK(Near(out[0].frame,0.5f*(kFlipFrames-1)));
    life.Step(1.5f);
    CHECK(life.Live()==0);
}

static void Ordering() {
    // Identity view (looking down -Z): farther = more negative z, drawn first.
    float view[16]{};view[0]=view[5]=view[10]=view[15]=1.0f;
    Instance in[4]{};
    in[0].pos[2]=-5.0f;in[1].pos[2]=-50.0f;in[2].pos[2]=-5.0f;in[3].pos[2]=-20.0f;
    std::uint32_t order[4];float keys[4];
    SortBackToFront(in,4,view,true,order,keys);
    CHECK(order[0]==1 && order[1]==3 && order[2]==0 && order[3]==2);   // stable for the equal pair
    SortBackToFront(in,4,view,false,order,keys);
    CHECK(order[0]==0 && order[1]==2 && order[2]==3 && order[3]==1);
    // A translated view: the camera at z=+10 (view = translate -10): depths shift, order holds.
    view[14]=-10.0f;
    CHECK(Near(ViewZ(view,in[0].pos),-15.0f));
}

static void Depth() {
    for(int r=0;r<2;++r) {
        float p[16];Perspective(p,1.7f,16.0f/9.0f,0.5f,20000.0f,r==1);
        CHECK(ReversedDepth(p,true)==(r==1));
        for(float z:{-0.6f,-3.0f,-75.0f,-1500.0f,-15000.0f}) {
            const float d=DepthFromViewZ(p,z);
            CHECK(d>=0.0f && d<=1.0f);
            const float back=ViewZFromDepth(p,d);
            CHECK(Near(back,z,std::fabs(z)*5e-3f+1e-3f));
        }
        CHECK(Near(DepthFromViewZ(p,-0.5f),r==1 ? 1.0f : 0.0f,1e-5f));
        CHECK(Near(DepthFromViewZ(p,-20000.0f),r==1 ? 0.0f : 1.0f,1e-5f));
    }
}

static void Flipbook() {
    std::vector<std::uint8_t> a(static_cast<std::size_t>(kFlipSide)*kFlipSide*4),b(a.size());
    const auto t0=std::chrono::steady_clock::now();
    GenerateSmokeFlipbook(a.data());
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    GenerateSmokeFlipbook(b.data());
    CHECK(a==b);
    const std::uint32_t h=Fnv(a.data(),a.size());
    std::printf("flipbook %dx%d, %d frames, %.0f ms, fnv %08X\n",kFlipSide,kFlipSide,kFlipFrames,ms,h);
    CHECK(h==0x9811A600u);   // the same bytes on every machine (a change to the generator changes this on purpose)
    CHECK(ms<2000.0);   // made once at plugin load on a worker thread
    // Every tile: a clear border of 2 px (no bleed between frames down to mip 4: 16 px tiles at mip 2 keep 0.5 px),
    // something drawn inside, and density not saturated everywhere.
    int empty=0;
    for(int f=0;f<kFlipFrames;++f) {
        const int ox=(f%kFlipGrid)*kFlipTile,oy=(f/kFlipGrid)*kFlipTile;
        unsigned border=0,sum=0,full=0;
        for(int y=0;y<kFlipTile;++y)for(int x=0;x<kFlipTile;++x) {
            const std::uint8_t alpha=a[(static_cast<std::size_t>(oy+y)*kFlipSide+static_cast<std::size_t>(ox+x))*4+3];
            const bool edge=x<2 || y<2 || x>=kFlipTile-2 || y>=kFlipTile-2;
            if(edge)border+=alpha;
            sum+=alpha;full+=alpha>=250;
        }
        CHECK(border==0);
        if(sum<kFlipTile*kFlipTile*8)++empty;   // mean alpha under 8/255
        CHECK(full<static_cast<unsigned>(kFlipTile*kFlipTile/2));
    }
    CHECK(empty==0);
    // Neighbouring frames are near each other (billowing, not flicker): mean absolute alpha change under 12/255.
    double change=0.0;
    for(int y=0;y<kFlipTile;++y)for(int x=0;x<kFlipTile;++x) {
        const auto at=[&](int f){const int ox=(f%kFlipGrid)*kFlipTile,oy=(f/kFlipGrid)*kFlipTile;
            return static_cast<int>(a[(static_cast<std::size_t>(oy+y)*kFlipSide+static_cast<std::size_t>(ox+x))*4+3]);};
        change+=std::abs(at(10)-at(11));
    }
    CHECK(change/(kFlipTile*kFlipTile)<12.0);
}

static void Determinism() {
    static Pool<64> p1,p2;
    Desc d{};
    const int i1=p1.Register(d),i2=p2.Register(d);
    Rng r1(42),r2(42);
    const float at[3]{1,2,3},v[3]{0,5,0};
    for(int i=0;i<64;++i){p1.Spawn(i1,at,v,r1);p2.Spawn(i2,at,v,r2);p1.Step(0.016f);p2.Step(0.016f);}
    Instance a[64],b[64];
    const int n1=p1.Build(a,64,kFlipFrames),n2=p2.Build(b,64,kFlipFrames);
    CHECK(n1==n2 && std::memcmp(a,b,sizeof(Instance)*static_cast<std::size_t>(n1))==0);
}

// The stock card: our corners land where the stock VS puts the same uv, for any turn; the stock grid table.
static void StockCard() {
    const float phi[4]={0.0f,1.5707963f,4.712389f,3.1415927f};          // stock corner k: uv (0,0) (1,0) (0,1) (1,1)
    const float cx[4]={-1.0f,1.0f,-1.0f,1.0f},cy[4]={1.0f,1.0f,-1.0f,-1.0f};   // ours, the same uv order
    for(float rot=-3.0f;rot<=3.0f;rot+=0.37f) {
        const float ours=StockRotToOurs(rot),s=std::sin(ours),c=std::cos(ours);
        for(int k=0;k<4;++k) {
            const float sx=std::sin(rot+phi[k]),sy=std::cos(rot+phi[k]);
            const float ox=(cx[k]*c-cy[k]*s)*0.70710678f,oy=(cx[k]*s+cy[k]*c)*0.70710678f;
            CHECK(Near(sx,ox,1e-4f) && Near(sy,oy,1e-4f));
        }
    }
    // Upright at stock rot -pi/4: uv (0,0) top left.
    CHECK(Near(StockRotToOurs(-0.78539816f),0.0f));
    CHECK(StockGrid(1)==1 && StockGrid(2)==1 && StockGrid(4)==2 && StockGrid(9)==3 && StockGrid(16)==4);
    CHECK(StockGrid(0)==0 && StockGrid(3)==0 && StockGrid(8)==0 && StockGrid(25)==0);
}

// The War Thunder style layers: which events make which layers, the counts' lod, the lifetimes, the budget.
static void Layers() {
    static Pool<4096> pool;
    const LayerDescs l=RegisterLayers(pool);
    CHECK(l.Ready() && l.flash==0 && l.bigDust==9);
    Rng rng(11);
    ShotEvent rifle{{0.0f,1.5f,0.0f},{0.0f,0.0f,1.0f},0.0f,0.0f,false};
    CHECK(SpawnShot(pool,l,rng,rifle,1.0f)==0 && pool.Live()==0);   // a small arm makes nothing
    ShotEvent tank{{0.0f,2.5f,0.0f},{0.0f,0.0f,1.0f},10.0f,0.0f,true};
    const ShotCounts tc=CountShot(tank,1.0f);
    CHECK(tc.flash==2 && tc.ring==14 && tc.dust==18 && tc.smoke==16);
    ShotEvent high=tank;high.pos[1]=30.0f;
    CHECK(CountShot(high,1.0f).dust==0);   // no dust ring off the ground
    ShotEvent launcher=tank;launcher.vehicle=false;
    const ShotCounts lc=CountShot(launcher,1.0f);
    CHECK(lc.flash==0 && lc.ring==0 && lc.dust==0 && lc.smoke==6);
    const ShotCounts far=CountShot(tank,0.25f);
    CHECK(far.ring<tc.ring && far.smoke<tc.smoke && far.smoke>=3);
    CHECK(Near(LodGrow(1.0f),1.0f) && Near(LodGrow(0.25f),2.0f) && Near(LodGrow(0.01f),LodGrow(0.2f)));
    // the blast size unit: linear to 6 m, then a square root (a 15 m rocket 9.5, a 40 m shell 15.5), never under 1
    CHECK(Near(BlastScale(0.5f),1.0f) && Near(BlastScale(6.0f),6.0f) && Near(BlastScale(24.0f),12.0f) && BlastScale(40.0f)<16.0f);
    const int made=SpawnShot(pool,l,rng,tank,1.0f);
    CHECK(made==tc.flash+tc.ring+tc.dust+tc.smoke && pool.Live()==made);
    // The flash and the ring are gone within a second; the smoke ball lives 8-20 s.
    pool.Step(1.0f);
    CHECK(pool.Live()==tc.dust+tc.smoke);
    pool.Step(6.5f);
    CHECK(pool.Live()==tc.smoke);
    std::vector<Instance> out(4096);
    int n=pool.Build(out.data(),4096,1);
    float low=1e9f;
    for(int i=0;i<n;++i)low=(std::min)(low,out[static_cast<std::size_t>(i)].pos[1]);
    CHECK(n==tc.smoke && low>2.5f);   // risen
    pool.Step(12.5f);
    CHECK(pool.Live()==0);
    pool.Clear();

    // Blasts: the heavy one adds the wide dust; the lod cuts counts, never to nothing.
    BlastEvent grenade{{0.0f,0.0f,0.0f},1.0f},shell{{0.0f,0.0f,0.0f},4.0f};
    const BlastCounts gc=CountBlast(grenade,1.0f),sc=CountBlast(shell,1.0f),sf=CountBlast(shell,0.2f);
    CHECK(gc.bigDust==0 && sc.bigDust>=3 && sc.black>gc.black);
    CHECK(sf.black>=2 && sf.black<sc.black && sf.bigDust>=3 && sf.fireball>=1);
    CHECK(CountBlast(BlastEvent{{0.0f,0.0f,0.0f},0.0f},1.0f).black==0);
    const int blast=SpawnBlast(pool,l,rng,shell,1.0f);
    CHECK(blast==sc.fireball+sc.dirt+sc.black+sc.debris+sc.residual+sc.bigDust && pool.Live()==blast);
    pool.Step(3.0f);   // fireball, dirt, debris gone
    CHECK(pool.Live()==sc.black+sc.residual+sc.bigDust);
    n=pool.Build(out.data(),4096,1);
    float top=-1e9f;
    for(int i=0;i<n;++i)top=(std::max)(top,out[static_cast<std::size_t>(i)].pos[1]);
    CHECK(top>10.0f);   // the black column rises
    pool.Step(16.0f);   // past 19 s: the black column is gone, the dust cloud (20-30 s) is not
    CHECK(pool.Live()>=sc.bigDust && pool.Live()<=sc.residual+sc.bigDust);
    pool.Step(12.0f);
    CHECK(pool.Live()==0);
    pool.Clear();

    // A heavy barrage stays in the budget: the pool replaces, never grows.
    for(int i=0;i<400;++i){BlastEvent b{{static_cast<float>(i),0.0f,0.0f},6.0f};SpawnBlast(pool,l,rng,b,LayerLod(80.0f,pool.Live(),4096));}
    CHECK(pool.Live()<=4096 && pool.Dropped()>0);
    CHECK(Near(LayerLod(30.0f,0,4096),1.0f) && Near(LayerLod(120.0f,0,4096),0.5f) && Near(LayerLod(1000.0f,0,4096),0.2f));
    CHECK(Near(LayerLod(30.0f,4000,4096),0.5f));
}

// The efs quad (gpufx_efs.h) against the stock VS 0x2130 transcribed step by step from its asm (corner rotation by
// sincos / dp2, the quaternion product, the basis, the world centre), the fetch into strip order, the basis.
static void EfsRefCorner(const EfsRecord& r,const EfsComposite& c,const float world[16],const float basis[9],float fx,float fy,
                         float out[3]) {
    const float s=std::sin(c.xrot),co=std::cos(c.xrot);
    const float r4x=-s,r4y=co,r4z=s;
    const float r3x=fy*r4x+fx*r4y,r3y=fy*r4y+fx*r4z;                 // dp2 r2.yx with r4.xy / r4.yz
    const float px=r3x*c.position[2]+c.position[0],py=r3y*c.position[3]+c.position[1];
    const float vx=r.scale[0]*px,vy=r.scale[1]*py;
    const float qx=r.quat[0],qy=r.quat[1],qz=r.quat[2],qw=r.quat[3];
    const float r0x=-qy*vx+vy*qx,r0z=qz*vx+qw*vy,r2y=-qx*vx-vy*qy,r0y=qw*vx-vy*qz;
    const float t[3]{r0y*qw+(-r2y*qx+(r0z*-qz+qy*r0x)),r0z*qw+(-r2y*qy+(r0x*-qx+qz*r0y)),r0x*qw+(-r2y*qz+(r0y*-qy+qx*r0z))};
    for(int k=0;k<3;++k)
        out[k]=t[0]*basis[k]+t[1]*basis[3+k]+t[2]*basis[6+k]+
               r.translate[0]*world[k]+r.translate[1]*world[4+k]+r.translate[2]*world[8+k]+world[12+k];
}

static void Efs() {
    // EfsBasis: the world's rows; a billboard the camera's rows times the world rows' lengths.
    float world[16]{2.0f,0.0f,0.0f,0.0f, 0.0f,0.0f,3.0f,0.0f, 0.0f,-4.0f,0.0f,0.0f, 5.0f,6.0f,7.0f,1.0f};
    float camera[16]{0.0f,1.0f,0.0f,0.0f, -1.0f,0.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 9.0f,9.0f,9.0f,1.0f};
    float basis[9];
    EfsBasis(world,nullptr,basis);
    CHECK(basis[0]==2.0f && basis[5]==3.0f && basis[7]==-4.0f && basis[1]==0.0f);
    EfsBasis(world,camera,basis);
    CHECK(Near(basis[1],2.0f) && Near(basis[3],-3.0f) && Near(basis[8],4.0f) && Near(basis[0],0.0f));
    // EfsQuad: every corner of a turned, scaled, offset, rotated quad as the asm computes it.
    std::uint32_t seed=12345u;
    auto rnd=[&]{seed=seed*1664525u+1013904223u;return static_cast<float>(seed>>8)/16777216.0f*2.0f-1.0f;};
    for(int it=0;it<200;++it) {
        for(int i=0;i<12;++i)if(i%4!=3 || i>=12)world[i]=rnd()*2.0f;
        world[12]=rnd()*50.0f;world[13]=rnd()*50.0f;world[14]=rnd()*50.0f;
        EfsRecord r{};EfsComposite c{};
        for(int k=0;k<3;++k)r.translate[k]=rnd()*4.0f;
        r.scale[0]=rnd()*3.0f;r.scale[1]=rnd()*3.0f;r.scale[2]=1.0f;
        float q[4]{rnd(),rnd(),rnd(),rnd()};const float ql=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
        for(int k=0;k<4;++k)r.quat[k]=q[k]/ql;
        c.xrot=rnd()*3.14159f;c.position[0]=rnd();c.position[1]=rnd();c.position[2]=rnd()*2.0f;c.position[3]=rnd()*2.0f;
        for(int k=0;k<4;++k){c.texcoord[k]=rnd();c.color[k]=rnd();}
        EfsBasis(world,(it&1) ? camera : nullptr,basis);
        Instance o[2];
        EfsQuad(r,c,world,basis,0.25f,1.5f,(it&2)!=0,o);
        CHECK(o[0].size==0.25f && o[0].emissive==1.5f && o[1].size==((it&2) ? 1.0f : 0.0f));
        CHECK(std::memcmp(o[0].color,c.color,sizeof(c.color))==0 && std::memcmp(o[1].color,c.texcoord,sizeof(c.texcoord))==0);
        const float corners[5][2]{{-1.0f,1.0f},{1.0f,1.0f},{-1.0f,-1.0f},{1.0f,-1.0f},{0.3f,-0.7f}};
        for(const auto& f:corners) {
            float ref[3];EfsRefCorner(r,c,world,basis,f[0],f[1],ref);
            const float ax[3]{o[0].rot,o[0].frame,o[0].soft};
            for(int k=0;k<3;++k)CHECK(Near(o[0].pos[k]+f[0]*ax[k]+f[1]*o[1].pos[k],ref[k],1e-3f));
        }
    }
    // EfsStripFetch: the stock corner order (uv (0,0) (1,0) (0,1) (1,1)) and a reordered one give the same strip.
    const float fetch[4][4]{{-0.5f,0.5f,0.0f,0.0f},{0.5f,0.5f,1.0f,0.0f},{-0.5f,-0.5f,0.0f,1.0f},{0.5f,-0.5f,1.0f,1.0f}};
    const std::uint32_t idx[6]{0,1,2,2,1,3};
    float strip[16];
    CHECK(EfsStripFetch(fetch,4,idx,strip));
    for(int v=0;v<4;++v)CHECK(std::memcmp(strip+4*v,fetch[v],sizeof(float)*4)==0);
    const float shuffled[5][4]{{9.0f,9.0f,9.0f,9.0f},{0.5f,-0.5f,1.0f,1.0f},{-0.5f,0.5f,0.0f,0.0f},{0.5f,0.5f,1.0f,0.0f},{-0.5f,-0.5f,0.0f,1.0f}};
    const std::uint32_t sidx[6]{2,3,4,4,3,1};
    float strip2[16];
    CHECK(EfsStripFetch(shuffled,5,sidx,strip2) && std::memcmp(strip,strip2,sizeof(strip))==0);
    // Refused: triangles sharing an edge that is not a diagonal, an index out of range, a degenerate triangle, an
    // off-centre rectangle, three corners.
    const std::uint32_t side[6]{0,1,2,0,2,3};CHECK(!EfsStripFetch(fetch,4,side,strip));
    const std::uint32_t past[6]{0,1,2,2,1,4};CHECK(!EfsStripFetch(fetch,4,past,strip));
    const std::uint32_t flat[6]{0,1,1,2,1,3};CHECK(!EfsStripFetch(fetch,4,flat,strip));
    const std::uint32_t three[6]{0,1,2,2,1,0};CHECK(!EfsStripFetch(fetch,4,three,strip));
    float off[4][4];std::memcpy(off,fetch,sizeof(off));off[1][0]=0.7f;off[3][0]=0.7f;
    CHECK(!EfsStripFetch(off,4,idx,strip));
}

int main() {
    Curves();Budget();Physics();Ordering();Depth();Flipbook();Determinism();StockCard();Layers();Efs();
    if(failures){std::printf("gpufx_sim: %d failure(s)\n",failures);return 1;}
    std::printf("gpufx_sim: ok\n");
    return 0;
}

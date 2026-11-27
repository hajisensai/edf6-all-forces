#pragma once
// 对象更新里「骨骼 -> 碰撞代理」同步（0x6ED800）的多线程引擎（docs/object-update-mt-re.md）。
// 纯逻辑：只依赖 Windows 与 SSE intrinsics，不碰插件的配置 / 日志 / 补丁，离线测试
// （tests/object_update_mt_test.cpp）直接对私有映射的 EDF.dll 跑同一份代码。
//
// 分工：
//  - 登记（游戏线程，slot5 里原来调 0x6ED800 的地方）：把容器排进任务队列，工作线程立刻开始算。
//  - 计算（工作线程，或 flush 时空闲的游戏线程）：每个 part 的 bind x 骨骼合成、0x511C0 正交化、
//    mode 2 的 0x97D9D0 变换；每个活动节点的 0x652F0 包围盒与并集。只读活数据，只写自己的结果区。
//  - 提交（游戏线程，flush 时按登记顺序）：照原版顺序回放所有写操作——锁、mode 2 的静止判断与
//    0x11B0AB0、mode 1 的 0x109B50、节点包围盒与 vt+0x88（0xE51570）、0x11B0940 / 0xDA08E0 或
//    0x11B1710、K+0xB4、解锁。算出错（异常、结果区不够）的任务在这里交回原版 0x6ED800 现算。
#include <Windows.h>
#include <immintrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>

#pragma warning(push)
#pragma warning(disable:4324)   // alignas 带来的结构体填充
namespace crew::objmt {
// ---- 原版代码地址（tests/object_update_mt_sig_audit.py 按名字读这些常量核对真 EDF.dll）----
constexpr unsigned kBonesToProxies=0x6ED800;     // 骨骼 -> 代理（被替代的整段）
constexpr unsigned kPhysicsToBones=0x6EDCA0;     // 代理 -> 骨骼
constexpr unsigned kProxyLock=0xDAC5B0,kProxyUnlock=0xDB04B0;   // 两个都是 ret 0
constexpr unsigned kOrthonormalize=0x511C0,kMatrixToTransform=0x97D9D0,kTransformAabb=0x652F0,kHasNan4=0x650E0;
constexpr unsigned kSetNodeMatrix=0x109B50,kCompoundUpdate=0x109B80,kPartTransform=0x6E9130;
constexpr unsigned kBodyLinear=0x11B1300,kBodyAngular=0x11B1060,kMotionMatches=0x11BAD40,kBodySetTransform=0x11B0AB0;
constexpr unsigned kBodyActivate=0x11B0940,kBodyDeactivate=0x11B1710,kShapeSetAabb=0xDA08E0;
constexpr unsigned kRestMask=0x1824540,kRestTolerance=0x1792EF0,kStepDt=0x179F358;
constexpr unsigned kAabbInitMin=0x1769300,kAabbInitMax=0x1769310,kAabbDefaultMin=0x1765C30,kAabbDefaultMax=0x1765B90;
constexpr unsigned kFilterWrapper=0x20B2970;     // [image+此处] +8 = hknpGroupCollisionFilter，+0x24 起 32 个层掩码
constexpr unsigned kCollisionFilter=0x1061E0;    // 过滤函数本身（只审计，不调用）

// 原版代码指纹（FNV-1a 64），安装前逐段核对。0x6EDC0C 的调用被 native_body_math 改过，跳开它。
struct CodeRange { unsigned begin,end; std::uint64_t fnv; };
constexpr CodeRange kCodeRanges[]{
    {0x6ED800,0x6EDC0C,0x2AD13D763DA350E6ull},{0x6EDC11,0x6EDC92,0x58D70AC3D2D8D66Aull},
    {0x109B80,0x109D93,0x5A5C1E5453175EA1ull},{0x109B50,0x109B7A,0xAE9EA9E428BFFDF8ull},
    {0x6E9130,0x6E92C1,0xA44CFB291B6F9096ull},{0x652F0,0x653E7,0x15B9F0F71A73C348ull},
    {0x511C0,0x514B6,0x5CC9765166287E42ull},{0x97D9D0,0x97DA0D,0x18C9CC0E2368432Bull},
    {0x650E0,0x650FB,0xEC28CF16ED9B2A8Dull},
};
inline std::uint64_t Fnv1a64(const unsigned char* p,std::size_t n) noexcept {
    std::uint64_t h=14695981039346656037ull;
    for(std::size_t i=0;i<n;++i)h=(h^p[i])*1099511628211ull;
    return h;
}

// ---- 改写的调用点 ----
constexpr unsigned kWindowOpen=0x11993CE;    // System::update 里 slot5 循环前的 call 0xDAC5B0
constexpr unsigned kWindowClose=0x119945B;   // 循环后的 call 0xDB04B0
constexpr unsigned kRegisterInsect=0x4A985C; // InsectBase：call 0x6ED800（rcx = rdi+0xB10）
constexpr unsigned kRegisterHuman=0x57584E;  // HumanBase：call 0x6ED800（rcx = rsi+0xD90）
constexpr unsigned char kCallLock[]{0xE8,0xDD,0x31,0xC1,0xFF};
constexpr unsigned char kCallUnlock[]{0xE8,0x50,0x70,0xC1,0xFF};
constexpr unsigned char kCallInsect[]{0xE8,0x9F,0x3F,0x24,0x00};
constexpr unsigned char kCallHuman[]{0xE8,0xAD,0x7F,0x17,0x00};

// ---- flush 原因 ----
enum class Reason : unsigned char {
    kWindowEnd,   // slot5 循环结束
    kRayQuery,    // 0x11A7EE0 射线（过滤后可能碰到代理层）
    kBoxQuery,    // 0x11A70B0 盒体投射（同上）
    kBodyQuery,   // 0x11A7480 / 0x11A77C0 对刚体的投射
    kPoseMode,    // 0x6ED480 / 0x6ED6B0 / 0x6E93B0：布娃娃 / 模式切换
    kSetPosition, // 0x11B1A00 刚体改位置
    kDelete,      // 0x118A1B0 删对象
    kRecall,      // 有待提交任务的容器被直接调 0x6ED800 / 0x6EDCA0
    kDuplicate,   // 同一容器同一窗口登记第二次
    kCapacity,    // 任务表或结果区快满
    kPlugin,      // 插件自己直接读写代理（enemy_control_native_pose FlushRag）
    kCount
};
constexpr const char* kReasonNames[]{"window","ray","box","body","pose","setpos","delete","recall","dup","capacity","plugin"};
static_assert(sizeof(kReasonNames)/sizeof(kReasonNames[0])==static_cast<unsigned>(Reason::kCount));

// ---- 闸门：函数中段插一段 cave（入口不能动：heli / edf41classes / *_net 会核对入口字节）----
// 每个站点：被挪走的原字节都不含 rip 相对寻址，rsp 16 对齐，标志位在此处死亡（或由挪走的指令自己设置）。
enum class Src : unsigned char { kRcx,kRdx,kR8 };
enum class Gate : unsigned char { kRay,kBox,kBody,kPose,kSetPosition,kDelete,kContainer };
struct GateSite { unsigned function,site; unsigned char size; Src src; Gate gate; unsigned char bytes[10]; };
constexpr GateSite kGateSites[]{
    {0x11A7EE0,0x11A7F1E,7,Src::kR8,Gate::kRay,{0x41,0x8B,0x40,0x20,0x49,0x8B,0xF8}},          // r8 = 射线输入，[r8+0x20] 过滤信息
    {0x11A70B0,0x11A70EE,6,Src::kRcx,Gate::kBox,{0x4D,0x8B,0xE1,0x4D,0x8B,0xF8}},              // rcx = 收集器，[rcx+0x10] 过滤信息
    {0x11A7480,0x11A74A7,7,Src::kRcx,Gate::kBody,{0x4C,0x8B,0xF1,0x4C,0x8B,0x69,0x58}},
    {0x11A77C0,0x11A77F1,6,Src::kRcx,Gate::kBody,{0x49,0x8B,0xD9,0x4D,0x8B,0xF8}},
    {0x6ED480,0x6ED48A,9,Src::kRcx,Gate::kPose,{0x8B,0x81,0x9C,0x00,0x00,0x00,0x48,0x8B,0xD9}},
    {0x6ED6B0,0x6ED6C5,6,Src::kRcx,Gate::kPose,{0x48,0x8B,0xE9,0x4C,0x8B,0xF2}},
    {0x6E93B0,0x6E93C5,10,Src::kRcx,Gate::kPose,{0x4C,0x8B,0xF1,0x48,0x8B,0x89,0xC0,0x00,0x00,0x00}},
    {0x11B1A00,0x11B1A1D,7,Src::kRcx,Gate::kSetPosition,{0x0F,0x10,0x0A,0x4C,0x8D,0x45,0x00}},
    {0x118A1B0,0x118A1BF,7,Src::kRcx,Gate::kDelete,{0x48,0x8B,0xD9,0xF6,0x41,0x18,0x04}},     // test 在恢复标志后执行
    {0x6ED800,0x6ED82D,10,Src::kRcx,Gate::kContainer,{0x48,0x8B,0xF1,0x48,0x8B,0x89,0xC0,0x00,0x00,0x00}},
    {0x6EDCA0,0x6EDCB2,10,Src::kRcx,Gate::kContainer,{0x48,0x8B,0x99,0xE0,0x00,0x00,0x00,0x48,0x8B,0xF9}},
};
constexpr unsigned kGateCount=sizeof(kGateSites)/sizeof(kGateSites[0]);
constexpr std::size_t kCaveMax=192;

// cave：pushfq / push rax；[pending]==0 直接走原字节；否则保存易失寄存器与 xmm0-5，rcx = 源寄存器，
// edx = arg，调 fn，全部恢复，执行挪走的原字节，jmp [rip] 回 ret。返回字节数，0 = 放不下。
inline std::size_t BuildGateCave(unsigned char* out,std::size_t cap,const volatile std::uint32_t* pending,Src src,
                                 std::uint32_t arg,const void* fn,const unsigned char* displaced,std::size_t n,
                                 const void* ret) noexcept {
    unsigned char b[kCaveMax];
    std::size_t k=0;
    const auto put=[&](std::initializer_list<unsigned char> bytes) noexcept {for(const auto x:bytes)b[k++]=x;};
    const auto put64=[&](const void* p) noexcept {const auto v=reinterpret_cast<std::uint64_t>(p);std::memcpy(b+k,&v,8);k+=8;};
    if(n>16)return 0;
    put({0x9C,0x50,0x48,0xB8});put64(const_cast<const std::uint32_t*>(pending));   // pushfq; push rax; mov rax,&pending
    put({0x83,0x38,0x00,0x74,0x00});                                                 // cmp dword [rax],0; je fast
    const std::size_t jeAt=k-1;
    put({0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53});                         // push rcx rdx r8 r9 r10 r11
    put({0x48,0x81,0xEC,0x80,0x00,0x00,0x00});                                       // sub rsp,0x80
    for(unsigned i=0;i<6;++i)put({0xF3,0x0F,0x7F,static_cast<unsigned char>(0x44+8*i),0x24,static_cast<unsigned char>(0x20+16*i)});
    if(src==Src::kRdx)put({0x48,0x89,0xD1});        // mov rcx,rdx
    else if(src==Src::kR8)put({0x4C,0x89,0xC1});    // mov rcx,r8
    b[k++]=0xBA;std::memcpy(b+k,&arg,4);k+=4;       // mov edx,arg
    put({0x48,0xB8});put64(fn);put({0xFF,0xD0});    // mov rax,fn; call rax
    for(unsigned i=0;i<6;++i)put({0xF3,0x0F,0x6F,static_cast<unsigned char>(0x44+8*i),0x24,static_cast<unsigned char>(0x20+16*i)});
    put({0x48,0x81,0xC4,0x80,0x00,0x00,0x00});                                       // add rsp,0x80
    put({0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,0x5A,0x59});                         // pop r11 r10 r9 r8 rdx rcx
    b[jeAt]=static_cast<unsigned char>(k-(jeAt+1));
    put({0x58,0x9D});                                                                // fast: pop rax; popfq
    std::memcpy(b+k,displaced,n);k+=n;
    put({0xFF,0x25,0,0,0,0});put64(ret);                                             // jmp [rip]
    if(k>cap)return 0;
    std::memcpy(out,b,k);
    return k;
}

// 过滤信息 info 的查询会不会碰到代理（层 5 节点、层 6 mode-2 部件）：照 0x1061E0 两个方向都看，
// 带组号（bit 15-30）时组逻辑可能放行，一律当会碰。
constexpr std::uint32_t kProxyLayerMask=(1u<<5)|(1u<<6);
inline bool FilterMayHitProxies(std::uint32_t info,const std::uint32_t* layers) noexcept {
    if(info&0x7FFF8000u)return true;
    const unsigned q=info&0x1Fu;
    return (layers[q]&kProxyLayerMask)!=0 || ((layers[5]>>q)&1u) || ((layers[6]>>q)&1u);
}

// ---- 容器布局（0x6ED800 / 0x109B80 的汇编）----
constexpr unsigned kCParts=0x78,kCPartCount=0x88,kCOwner=0xC0,kCCompound=0x110,kPartStride=0xC0;
constexpr unsigned kPartBind=8,kPartBody=0x50,kPartMode=0x9C,kPartActive=0xA4,kPartNode=0xB0;
constexpr unsigned kKWorldBody=0x68,kKShape=0x78,kKAabb=0x80,kKNodes=0xA0,kKDone=0xB4;
constexpr unsigned kNodeBody=0x10,kNodeLocal=0x20,kNodeBox=0x40,kNodeMatrix=0x60,kNodePos=0x90,kNodeActive=0xA0;
constexpr unsigned kBodyId=0xF0,kBodyHolder=0x100,kHolderWorld=0x58;   // [[body+0x100]+0x58] = H；H+0x18 世界，H+0x20 运动
constexpr std::size_t kMaxParts=1024,kMaxNodes=1024;

template<class T> inline T At(const void* p,std::size_t off) noexcept {T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof(T));return v;}

// 原版的合成：row_i = ((b[4i]·R0 + b[4i+1]·R1) + b[4i+2]·R2) + b[4i+3]·R3，R = 骨骼 +0xB0..+0xEF。
// 原版先算 b[4i+1]·R1 再加 b[4i]·R0，加法可交换，逐位相同。
inline void Compose(const float* bind,const unsigned char* bone,__m128* m) noexcept {
    const __m128 r0=_mm_loadu_ps(reinterpret_cast<const float*>(bone+0xB0)),r1=_mm_loadu_ps(reinterpret_cast<const float*>(bone+0xC0));
    const __m128 r2=_mm_loadu_ps(reinterpret_cast<const float*>(bone+0xD0)),r3=_mm_loadu_ps(reinterpret_cast<const float*>(bone+0xE0));
    for(unsigned i=0;i<4;++i) {
        __m128 v=_mm_add_ps(_mm_mul_ps(_mm_set1_ps(bind[4*i+1]),r1),_mm_mul_ps(_mm_set1_ps(bind[4*i]),r0));
        v=_mm_add_ps(v,_mm_mul_ps(_mm_set1_ps(bind[4*i+2]),r2));
        m[i]=_mm_add_ps(v,_mm_mul_ps(_mm_set1_ps(bind[4*i+3]),r3));
    }
}
// 原版的静止判断：|v - 0| <= tol 的前三分量都成立（NaN 不成立）。
inline bool AtRest(const float* v,__m128 signMask,__m128 tolerance) noexcept {
    const __m128 x=_mm_sub_ps(_mm_loadu_ps(v),_mm_setzero_ps());
    return (_mm_movemask_ps(_mm_cmple_ps(_mm_andnot_ps(signMask,x),tolerance))&7)==7;
}
inline bool HasNan4(const float* v) noexcept {for(unsigned i=0;i<4;++i)if(v[i]!=v[i])return true;return false;}
// 0x109B80 的并集一步：候选 min = (c-h).xyz、max = (c+h).xyz，minps / maxps（NaN 取候选），只写回 xyz。
inline void Accumulate(float* mn,float* mx,const float* box) noexcept {
    const __m128 c=_mm_loadu_ps(box),h=_mm_loadu_ps(box+4);
    alignas(16) float lo[4],hi[4];
    _mm_store_ps(lo,_mm_min_ps(_mm_loadu_ps(mn),_mm_sub_ps(c,h)));
    _mm_store_ps(hi,_mm_max_ps(_mm_loadu_ps(mx),_mm_add_ps(c,h)));
    for(unsigned i=0;i<3;++i){mn[i]=lo[i];mx[i]=hi[i];}
}

// ---- 原版函数 ----
struct Stock {
    void(*bonesToProxies)(void*);
    void(*lock)(void*);
    void(*unlock)(void*);
    void(*orthonormalize)(void*);
    void(*matrixToTransform)(void*,const void*);
    void(*transformAabb)(void*,const void*,const void*);
    void(*setNodeMatrix)(void*,void*,const void*);
    void(*compoundUpdate)(void*);
    void(*partTransform)(void*,void*);
    const float*(*bodyLinear)(void*);
    const float*(*bodyAngular)(void*,void*);
    bool(*motionMatches)(void*,const void*);
    void(*bodySetTransform)(void*,const void*,float);
    void(*bodyActivate)(void*);
    void(*bodyDeactivate)(void*);
    void(*shapeSetAabb)(void*,void*);
    __m128 restMask,restTolerance;
    float stepDt;
    float aabbInit[8],aabbDefault[8];
};
template<class F> inline void Bind(F& f,unsigned char* image,unsigned rva) noexcept {f=reinterpret_cast<F>(image+rva);}
inline Stock ResolveStock(unsigned char* image) noexcept {
    Stock s{};
    Bind(s.bonesToProxies,image,kBonesToProxies);Bind(s.lock,image,kProxyLock);Bind(s.unlock,image,kProxyUnlock);
    Bind(s.orthonormalize,image,kOrthonormalize);Bind(s.matrixToTransform,image,kMatrixToTransform);
    Bind(s.transformAabb,image,kTransformAabb);Bind(s.setNodeMatrix,image,kSetNodeMatrix);
    Bind(s.compoundUpdate,image,kCompoundUpdate);Bind(s.partTransform,image,kPartTransform);
    Bind(s.bodyLinear,image,kBodyLinear);Bind(s.bodyAngular,image,kBodyAngular);Bind(s.motionMatches,image,kMotionMatches);
    Bind(s.bodySetTransform,image,kBodySetTransform);Bind(s.bodyActivate,image,kBodyActivate);
    Bind(s.bodyDeactivate,image,kBodyDeactivate);Bind(s.shapeSetAabb,image,kShapeSetAabb);
    s.restMask=_mm_loadu_ps(reinterpret_cast<const float*>(image+kRestMask));
    s.restTolerance=_mm_loadu_ps(reinterpret_cast<const float*>(image+kRestTolerance));
    std::memcpy(&s.stepDt,image+kStepDt,4);
    std::memcpy(s.aabbInit,image+kAabbInitMin,16);std::memcpy(s.aabbInit+4,image+kAabbInitMax,16);
    std::memcpy(s.aabbDefault,image+kAabbDefaultMin,16);std::memcpy(s.aabbDefault+4,image+kAabbDefaultMax,16);
    return s;
}
inline unsigned char* Holder(void* body) noexcept {return At<unsigned char*>(At<unsigned char*>(body,kBodyHolder),kHolderWorld);}
using MotionOf=void*(*)(void*,unsigned);
using SetPose=void(*)(void*,unsigned,const void*,const double*,int);

// ---- 结果 ----
struct alignas(16) Action { float m[16]; void* target; void* part; unsigned mode; };   // mode 1：m = M，target = 节点；mode 2：m = T，target = 刚体
struct alignas(16) NodeOut { float box[8]; void* node; };                             // 0x652F0 的输出（写到 node+0x40..+0x5F）
struct alignas(16) Result {
    float aabb[8];        // 并集（已按 0x650E0 换成默认值）
    Action* actions;
    NodeOut* nodes;
    unsigned actionCount,nodeCount;
    unsigned status;      // kOk / kFault / kOverflow
    bool compound;        // 计算时 K+0x68 非空
    bool any;             // 有活动节点
};
enum : unsigned { kOk=0,kFault=1,kOverflow=2,kRunning=3 };

// 结果区：一次性大块，原子 bump，窗口内不挪不回收，flush 完整体清零。
struct Arena {
    unsigned char* base=nullptr;
    std::size_t size=0;
    std::atomic<std::size_t> used{0};
    void* Take(std::size_t n) noexcept {
        n=(n+15)&~std::size_t{15};
        const std::size_t at=used.fetch_add(n,std::memory_order_relaxed);
        return at+n<=size ? base+at : nullptr;
    }
};

// 纯计算（不含需要析构的对象，可在 __try 里调）。只读容器、part、bind、骨骼、节点；只写 r 与 arena。
inline unsigned Compute(const Stock& s,unsigned char* c,Result& r,Arena& arena) noexcept {
    r.actions=nullptr;r.nodes=nullptr;r.actionCount=r.nodeCount=0;r.compound=r.any=false;
    auto* const parts=At<unsigned char*>(c,kCParts);
    const auto count=At<std::uint64_t>(c,kCPartCount);
    if(count>kMaxParts)return kOverflow;
    if(count) {
        r.actions=static_cast<Action*>(arena.Take(count*sizeof(Action)));
        if(!r.actions)return kOverflow;
    }
    for(std::uint64_t i=0;i<count;++i) {
        auto* const part=parts+i*kPartStride;
        if(!part[kPartActive])continue;
        const auto mode=At<unsigned>(part,kPartMode);
        if(mode!=1 && mode!=2)continue;
        const auto* const bind=At<const float*>(part,kPartBind);
        const auto* const bone=At<const unsigned char*>(At<const void*>(bind,0x40),0);
        __m128 m[4];
        Compose(bind,bone,m);
        s.orthonormalize(m);
        Action& a=r.actions[r.actionCount++];
        a.part=part;a.mode=mode;
        if(mode==2) {
            s.matrixToTransform(a.m,m);
            a.target=At<void*>(part,kPartBody);
        } else {
            _mm_store_ps(a.m,m[0]);_mm_store_ps(a.m+4,m[1]);_mm_store_ps(a.m+8,m[2]);_mm_store_ps(a.m+12,m[3]);
            a.target=At<void*>(part,kPartNode);
        }
    }
    auto* const k=c+kCCompound;
    if(!At<void*>(k,kKWorldBody))return kOk;
    r.compound=true;
    auto* const sentinel=At<unsigned char*>(k,kKNodes);
    unsigned total=0,active=0;
    for(auto* n=At<unsigned char*>(sentinel,0);n!=sentinel;n=At<unsigned char*>(n,0)) {
        if(++total>kMaxNodes)return kOverflow;
        if(n[kNodeActive])++active;
    }
    std::memcpy(r.aabb,s.aabbInit,sizeof(r.aabb));
    if(!active)return kOk;
    r.nodes=static_cast<NodeOut*>(arena.Take(active*sizeof(NodeOut)));
    if(!r.nodes)return kOverflow;
    for(auto* n=At<unsigned char*>(sentinel,0);n!=sentinel;n=At<unsigned char*>(n,0)) {
        if(!n[kNodeActive])continue;
        const void* matrix=n+kNodeMatrix;   // 没有 mode-1 part 指向它：节点自己的矩阵
        for(unsigned j=r.actionCount;j-->0;)if(r.actions[j].mode==1 && r.actions[j].target==n){matrix=r.actions[j].m;break;}
        NodeOut& o=r.nodes[r.nodeCount++];
        o.node=n;
        s.transformAabb(o.box,n+kNodeLocal,matrix);
        Accumulate(r.aabb,r.aabb+4,o.box);
    }
    r.any=true;
    if(HasNan4(r.aabb) || HasNan4(r.aabb+4))std::memcpy(r.aabb,s.aabbDefault,sizeof(r.aabb));
    return kOk;
}

// 两份结果是否逐位相同；不同时 what 给出第一处。
inline bool SameResult(const Result& a,const Result& b,const char** what) noexcept {
    const auto fail=[&](const char* w) noexcept {if(what)*what=w;return false;};
    if(a.status!=b.status)return fail("status");
    if(a.status!=kOk)return true;
    if(a.actionCount!=b.actionCount)return fail("part count");
    for(unsigned i=0;i<a.actionCount;++i) {
        const Action &x=a.actions[i],&y=b.actions[i];
        if(x.mode!=y.mode || x.target!=y.target || x.part!=y.part)return fail("part target");
        if(std::memcmp(x.m,y.m,sizeof(x.m)))return fail(x.mode==2 ? "body transform" : "node matrix");
    }
    if(a.compound!=b.compound || a.any!=b.any || a.nodeCount!=b.nodeCount)return fail("node count");
    for(unsigned i=0;i<a.nodeCount;++i) {
        if(a.nodes[i].node!=b.nodes[i].node)return fail("node order");
        if(std::memcmp(a.nodes[i].box,b.nodes[i].box,sizeof(a.nodes[i].box)))return fail("node box");
    }
    if(a.compound && std::memcmp(a.aabb,b.aabb,sizeof(a.aabb)))return fail("union");
    return true;
}

// 提交：游戏线程，照原版 0x6ED800 + 0x109B80 的顺序回放写操作。prefetch 保留 0x6EDC0C 补丁的效果；
// 节点的 vt+0x88 就是 0xE51570，0xE51651 / 0xE51683 两处补丁照旧生效。
inline void Commit(const Stock& s,unsigned char* c,const Result& r) noexcept {
    void* const lockArg=At<void*>(At<unsigned char*>(c,kCOwner),0x58);
    s.lock(lockArg);
    for(unsigned i=0;i<r.actionCount;++i) {
        const Action& a=r.actions[i];
        if(a.mode==2) {
            void* const body=a.target;
            alignas(16) float scratch[4];
            if(AtRest(s.bodyLinear(body),s.restMask,s.restTolerance) &&
               AtRest(s.bodyAngular(body,scratch),s.restMask,s.restTolerance)) {
                auto* const motion=Holder(body)+0x20;
                void* const hb=At<MotionOf>(At<void*>(motion,0),0x80)(motion,At<unsigned>(body,kBodyId));
                if(s.motionMatches(hb,a.m))continue;
            }
            s.bodySetTransform(body,a.m,s.stepDt);
        } else {
            const auto* const nodeBody=At<const char*>(a.target,kNodeBody);
            _mm_prefetch(nodeBody+kBodyId,_MM_HINT_T0);
            _mm_prefetch(nodeBody+kBodyHolder,_MM_HINT_T0);
            s.setNodeMatrix(c+kCCompound,a.target,a.m);
        }
    }
    auto* const k=c+kCCompound;
    if(At<void*>(k,kKWorldBody)) {
        if(!r.compound)s.compoundUpdate(k);   // 计算时还没有世界刚体：交给原版现算
        else {
            for(unsigned i=0;i<r.nodeCount;++i) {
                auto* const n=static_cast<unsigned char*>(r.nodes[i].node);
                std::memcpy(n+kNodeBox,r.nodes[i].box,sizeof(r.nodes[i].box));
                void* const body=At<void*>(n,kNodeBody);
                auto* const world=Holder(body)+0x18;
                alignas(16) double pos[4];
                const __m128 t=_mm_loadu_ps(reinterpret_cast<const float*>(n+kNodePos));
                _mm_store_pd(pos,_mm_cvtps_pd(t));
                _mm_store_pd(pos+2,_mm_cvtps_pd(_mm_movehl_ps(t,t)));
                At<SetPose>(At<void*>(world,0),0x88)(world,At<unsigned>(body,kBodyId),n+kNodeMatrix,pos,1);
            }
            void* const worldBody=At<void*>(k,kKWorldBody);
            if(r.any) {
                s.bodyActivate(worldBody);
                alignas(16) float box[8];
                std::memcpy(box,r.aabb,sizeof(box));
                s.shapeSetAabb(At<void*>(k,kKShape),box);
                std::memcpy(k+kKAabb,box,sizeof(box));
            } else s.bodyDeactivate(worldBody);
            k[kKDone]=1;
        }
    }
    s.unlock(lockArg);
}

// ---- 待提交集合（指针开放寻址，按代清空）----
struct PendingSet {
    static constexpr unsigned kSlots=4096;
    struct Slot { const void* key; unsigned gen; };
    Slot slots[kSlots]{};
    unsigned gen=1;
    static unsigned Hash(const void* p) noexcept {auto v=reinterpret_cast<std::uintptr_t>(p);v^=v>>17;v*=0x9E3779B97F4A7C15ull;return static_cast<unsigned>(v>>40);}
    bool Contains(const void* p) const noexcept {
        for(unsigned i=Hash(p),n=0;n<kSlots;++n,++i) {const Slot& s=slots[i%kSlots];if(s.gen!=gen)return false;if(s.key==p)return true;}
        return false;
    }
    bool Insert(const void* p) noexcept {   // false：已在里面
        for(unsigned i=Hash(p),n=0;n<kSlots;++n,++i) {
            Slot& s=slots[i%kSlots];
            if(s.gen!=gen){s.key=p;s.gen=gen;return true;}
            if(s.key==p)return false;
        }
        return false;
    }
    void Clear() noexcept {if(++gen==0){std::memset(slots,0,sizeof(slots));gen=1;}}
};

// ---- 引擎 ----
struct Stats {
    unsigned registered=0,inlineCalls=0,committed=0,handedBack=0,faults=0,overflows=0,ranByGame=0,shadowed=0,mismatches=0;
    unsigned flushes[static_cast<unsigned>(Reason::kCount)]{};
    long long commitTicks=0,waitTicks=0;
    std::atomic<long long> computeTicks{0};
    void Reset() noexcept {
        registered=inlineCalls=committed=handedBack=faults=overflows=ranByGame=shadowed=mismatches=0;
        for(auto& f:flushes)f=0;
        commitTicks=waitTicks=0;computeTicks.store(0,std::memory_order_relaxed);
    }
};

struct Engine {
    static constexpr unsigned kMaxTasks=1024,kMaxThreads=8;
    static constexpr std::size_t kArenaBytes=std::size_t{16}<<20,kShadowArenaBytes=std::size_t{2}<<20;
    struct Task { unsigned char* c; unsigned mxcsr; std::atomic<unsigned> done; Result r; Result* shadow; };

    Stock stock{};
    Task* tasks=nullptr;
    Arena arena,shadowArena;
    PendingSet* pendingSet=nullptr;
    // 队列：bit 48.. 代，bit 24..47 已发布，bit 0..23 已认领。工作线程 CAS 认领，代号防止跨 flush 的旧 CAS 成功。
    std::atomic<std::uint64_t> queue{0};
    alignas(64) volatile std::uint32_t pending=0;   // 已登记未提交的任务数（cave 的快速路径读）
    std::atomic<bool> active{false},stop{false};
    HANDLE wake=nullptr;
    HANDLE threads[kMaxThreads]{};
    unsigned threadCount=0;
    DWORD gameThread=0;
    bool open=false,inFlush=false,disabled=false;   // disabled：出过一次异常，此后全交原版
    unsigned shadowEvery=0,shadowTick=0;
    // 回调（可空）：工作线程异常（在 __except 过滤器里调，写 minidump）；影子比对不一致。
    void(*onFault)(EXCEPTION_POINTERS*,void* container)=nullptr;
    void(*onMismatch)(void* container,const char* what,const char* when)=nullptr;
    Stats stats;

    static unsigned Claimed(std::uint64_t q) noexcept {return static_cast<unsigned>(q&0xFFFFFF);}
    static unsigned Published(std::uint64_t q) noexcept {return static_cast<unsigned>((q>>24)&0xFFFFFF);}

    bool Init(unsigned char* image,unsigned workers) noexcept {
        stock=ResolveStock(image);
        const std::size_t taskBytes=sizeof(Task)*kMaxTasks;
        auto* const mem=static_cast<unsigned char*>(VirtualAlloc(nullptr,taskBytes+kArenaBytes+kShadowArenaBytes+sizeof(PendingSet),
                                                                 MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!mem)return false;
        tasks=reinterpret_cast<Task*>(mem);
        arena.base=mem+taskBytes;arena.size=kArenaBytes;
        shadowArena.base=arena.base+kArenaBytes;shadowArena.size=kShadowArenaBytes;
        pendingSet=new(shadowArena.base+kShadowArenaBytes) PendingSet();
        wake=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!wake)return false;
        if(workers>kMaxThreads)workers=kMaxThreads;
        for(unsigned i=0;i<workers;++i) {
            threads[threadCount]=CreateThread(nullptr,256*1024,&Worker,this,0,nullptr);
            if(threads[threadCount])++threadCount;
        }
        return true;
    }
    void Shutdown() noexcept {   // 只给测试用：游戏里线程随进程走
        stop.store(true);
        if(wake)SetEvent(wake);
        for(unsigned i=0;i<threadCount;++i){WaitForSingleObject(threads[i],INFINITE);CloseHandle(threads[i]);}
        threadCount=0;
    }

    // 跑一个已认领的任务（工作线程或游戏线程）。
    void Run(unsigned index) noexcept {
        Task& t=tasks[index];
        const unsigned saved=_mm_getcsr();
        _mm_setcsr(t.mxcsr);
        LARGE_INTEGER a,b;
        QueryPerformanceCounter(&a);
        t.r.status=Guarded(t.c,t.r,arena);
        QueryPerformanceCounter(&b);
        _mm_setcsr(saved);
        stats.computeTicks.fetch_add(b.QuadPart-a.QuadPart,std::memory_order_relaxed);
        t.done.store(1,std::memory_order_release);
    }
    unsigned Guarded(unsigned char* c,Result& r,Arena& a) noexcept {
        __try {
            return Compute(stock,c,r,a);
        } __except(Filter(GetExceptionInformation(),c)) {
            return kFault;
        }
    }
    int Filter(EXCEPTION_POINTERS* info,void* c) noexcept {
        if(onFault)onFault(info,c);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    bool TryClaim(unsigned& index) noexcept {
        std::uint64_t q=queue.load(std::memory_order_acquire);
        while(Claimed(q)<Published(q)) {
            if(queue.compare_exchange_weak(q,q+1,std::memory_order_acq_rel,std::memory_order_acquire)){index=Claimed(q);return true;}
        }
        return false;
    }
    static DWORD WINAPI Worker(void* self) noexcept {
        auto* const e=static_cast<Engine*>(self);
        unsigned idle=0;
        while(!e->stop.load(std::memory_order_relaxed)) {
            unsigned index;
            if(e->TryClaim(index)){e->Run(index);idle=0;continue;}
            if(e->active.load(std::memory_order_acquire)) {
                if(++idle<256)_mm_pause();
                else SwitchToThread();
                continue;
            }
            WaitForSingleObject(e->wake,INFINITE);
        }
        return 0;
    }

    // ---- 游戏线程 ----
    void Open() noexcept {gameThread=GetCurrentThreadId();open=!disabled;}
    void Close() noexcept {
        Flush(Reason::kWindowEnd);
        open=false;
        if(active.exchange(false,std::memory_order_acq_rel))ResetEvent(wake);
    }
    bool OnGameThread() const noexcept {return GetCurrentThreadId()==gameThread;}
    bool IsPending(const void* c) const noexcept {return pending && pendingSet->Contains(c);}

    void Register(unsigned char* c) noexcept {
        if(!open || inFlush || !OnGameThread()){++stats.inlineCalls;stock.bonesToProxies(c);return;}
        if(pending && pendingSet->Contains(c))Flush(Reason::kDuplicate);
        if(pending>=kMaxTasks || arena.used.load(std::memory_order_relaxed)>kArenaBytes/4*3)Flush(Reason::kCapacity);
        const unsigned index=pending;
        Task& t=tasks[index];
        t.c=c;t.mxcsr=_mm_getcsr();t.done.store(0,std::memory_order_relaxed);t.r.status=kRunning;t.shadow=nullptr;
        if(shadowEvery && ++shadowTick>=shadowEvery) {
            shadowTick=0;
            auto* const shadow=static_cast<Result*>(shadowArena.Take(sizeof(Result)));
            if(shadow){shadow->status=Guarded(c,*shadow,shadowArena);t.shadow=shadow;}
        }
        pendingSet->Insert(c);
        pending=index+1;
        ++stats.registered;
        queue.fetch_add(std::uint64_t{1}<<24,std::memory_order_release);
        if(!active.load(std::memory_order_relaxed)){active.store(true,std::memory_order_release);SetEvent(wake);}
    }

    void Flush(Reason why) noexcept {
        if(!pending || inFlush)return;
        inFlush=true;
        ++stats.flushes[static_cast<unsigned>(why)];
        LARGE_INTEGER t0,t1,t2;
        long long wait=0,commit=0;
        const unsigned n=pending;
        for(unsigned i=0;i<n;++i) {
            Task& t=tasks[i];
            if(!t.done.load(std::memory_order_acquire)) {
                QueryPerformanceCounter(&t0);
                while(!t.done.load(std::memory_order_acquire)) {
                    unsigned index;
                    if(TryClaim(index)){Run(index);++stats.ranByGame;}
                    else _mm_pause();
                }
                QueryPerformanceCounter(&t1);
                wait+=t1.QuadPart-t0.QuadPart;
            }
            QueryPerformanceCounter(&t1);
            CommitTask(t);
            QueryPerformanceCounter(&t2);
            commit+=t2.QuadPart-t1.QuadPart;
        }
        stats.waitTicks+=wait;stats.commitTicks+=commit;
        // 全部认领且完成：换代清零。
        const std::uint64_t q=queue.load(std::memory_order_acquire);
        queue.store(((q>>48)+1)<<48,std::memory_order_release);
        arena.used.store(0,std::memory_order_relaxed);
        shadowArena.used.store(0,std::memory_order_relaxed);
        pendingSet->Clear();
        pending=0;
        inFlush=false;
    }
    void CommitTask(Task& t) noexcept {
        if(t.r.status!=kOk) {
            ++stats.handedBack;
            if(t.r.status==kFault){++stats.faults;disabled=true;}else ++stats.overflows;
            stock.bonesToProxies(t.c);   // 交回原版现算（经 0x6ED82D 的闸门：inFlush，直接放行）
            return;
        }
        if(t.shadow)CheckShadow(t);
        Commit(stock,t.c,t.r);
        ++stats.committed;
    }
    // Debug：登记时游戏线程现算的一份（A）、提交前再现算的一份（B）都要和工作线程的（W）逐位相同；
    // 每个 part 的 T 还要和原版 0x6E9130 相同。
    void CheckShadow(Task& t) noexcept {
        if(t.shadow->status==kOverflow)return;   // 影子结果区不够：这次不比
        ++stats.shadowed;
        const char* what=nullptr;
        if(!SameResult(t.r,*t.shadow,&what)){++stats.mismatches;if(onMismatch)onMismatch(t.c,what,"registration");return;}
        auto* const now=static_cast<Result*>(shadowArena.Take(sizeof(Result)));
        if(now) {
            now->status=Guarded(t.c,*now,shadowArena);
            if(now->status!=kOverflow && !SameResult(t.r,*now,&what)){++stats.mismatches;if(onMismatch)onMismatch(t.c,what,"commit");return;}
        }
        for(unsigned i=0;i<t.r.actionCount;++i) {
            const Action& a=t.r.actions[i];
            alignas(16) float stockT[16],mineT[16];
            stock.partTransform(a.part,stockT);
            if(a.mode==2)std::memcpy(mineT,a.m,sizeof(mineT));
            else stock.matrixToTransform(mineT,a.m);
            if(std::memcmp(stockT,mineT,sizeof(mineT))){++stats.mismatches;if(onMismatch)onMismatch(t.c,"part transform","0x6E9130");return;}
        }
    }
};
}  // namespace crew::objmt
#pragma warning(pop)

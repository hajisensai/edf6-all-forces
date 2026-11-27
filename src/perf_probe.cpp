// The measurement probes (perf_probe.h; docs/plan-2026-11-24-optimization.md section 3 A).
#include "perf_probe.h"

#include <dxgi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>

#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "scene.h"
#include "ground_navigation.h"
#include "memory.h"
#include "edf/patch.h"
#include "net_counters.h"

namespace crew::perfprobe {
namespace {
// The call sites (E8 rel32) and the stock function each calls (EDF.dll, checked by disassembly 2026-11-24).
// The main loop 0x11A2E90 (its frame loop 0x11A2ECA..0x11A3116): tick wait (0x11A2F43..0x11A2F74), the frame's input sample, Application::Update (vtable +8 at
// 0x11A3006), the render fence wait, the Present command.
constexpr unsigned kInputCall=0x11A2FF3,kInput=0x1182A10;
constexpr unsigned kFenceCall=0x11A305A,kFence=0x111CC20;
constexpr unsigned kPresentCall=0x11A309B,kPresentCmd=0x111D890;
// Application::Update 0x705530 -> System::update (rcx system, xmm1 dt, r8d steps); in it the frame's render recording
// (before the steps), and in that Umbra's resolveVisibility (an import jump, 0x11B6B1D).
constexpr unsigned kUpdateCall=0x705715,kUpdate=0x1198DA0;
constexpr unsigned kDrawCall=0x1198FA4,kDraw=0x11978D0;
constexpr unsigned kResolveCall=0x11B6B1D,kResolve=0x12D6110;
// System::update steps Havok before and after object updates. Each native step runs two graphs.
constexpr unsigned kHavokBeforeCall=0x1199359,kHavokAfterCall=0x1199479,kHavokStep=0x11AD780;
constexpr unsigned kHavokBuildCall=0x11AD814,kHavokBuild=0xDAF820;
constexpr unsigned kHavokFinishCall=0x11AD827,kHavokFinish=0xDAFA30;
constexpr unsigned kCollideConstructCall=0xDAF931,kCollideConstruct=0xDAAD40;
constexpr unsigned kCollideDispatchCalls[]={0xDAF9BB,0xDAF9E0};
constexpr unsigned kCollideSleepCall=0xA0FC48,kCollideSleep=0x9EAE60;
constexpr unsigned kDefaultQueueInterface=0x182EC80,kDefaultQueueMode=0x36C;
constexpr unsigned kModeZeroExecute=0xA0FE74;
constexpr unsigned kPostCollideVtable=0x18C1D28;
constexpr unsigned kContactDispatchCall=0xDAC932,kContactDispatch=0xE781D0;
constexpr unsigned kPostCollideSignalCall=0xDACA04,kPostCollideSignal=0xDA9F60;
constexpr unsigned kEventCommandCall=0xE77770,kEventCommand=0x12FE960;
constexpr unsigned kEventListenerCall=0xE75FBB;
constexpr unsigned char kEventListenerSig[]={0x4C,0x8B,0xC5,0x49,0x8B,0xD7,0x41,0xFF,0x51,0x18};
constexpr unsigned kGraphNodes=0x18,kGraphNodeCount=0x20,kGraphEdges=0x328,kGraphEdgeCount=0x330;
constexpr unsigned kGraphNodeStride=0x18,kGraphNodeJob=0x10;
constexpr unsigned char kTaskExecuteSig[]={0x48,0x8D,0x43,0x10,0x48,0x89,0x45,0x88,0x48,0x8D,0x55,
                                            0xA0,0x48,0x8B,0x4B,0x58,0x48,0x8B,0x01,0xFF,0x50,0x10};
// mov rax,[rsi]; lea rdx,[rsp+0x20]; xor r8d,r8d; mov rcx,rsi; call [rax+0xA8]
constexpr unsigned char kCollideDispatchSig[]={0x48,0x8B,0x06,0x48,0x8D,0x54,0x24,0x20,0x45,0x33,0xC0,
                                               0x48,0x8B,0xCE,0xFF,0x90,0xA8,0,0,0};
// The draw submits (system, object, pass in r8d) of the scene's lists, and the mesh draws.
constexpr unsigned kSubmitCalls[]={0x11E255C,0x11E25F9,0x11E3A3F,0x11E4029,0x11E40BC};
constexpr unsigned kSubmit=0x11E1630;
constexpr unsigned kMeshCalls[]={0x1103FD1,0x6C30CB};
constexpr unsigned kMesh=0x10FF4E0;
// Render thread (G line, gpu-5 / gpu-6). Umbra's occlusion query read 0x1631790 (rcx renderer, rdx query, r9 result,
// [rsp+0x20] wait byte; GetData(query,&r,8,flags 0) at 0x1631860, al = the result is in), its one caller the
// Commander's query loop at 0x11E4244. The skin palette 0x11009A0 (inverse bind x pose, then UpdateSubresource of the
// whole buffer), called from the LOD draw 0x1104100 once a pass at 0x1104131.
constexpr unsigned kQueryCall=0x11E4244,kQuery=0x1631790;
constexpr unsigned kPaletteCall=0x1104131,kPalette=0x11009A0;
// The renderer (lea 0x2136FF0, the Present command's rcx) and its swap chain: the render thread's Present command
// 0x1118AF0 calls [[renderer+0x90]]+0x40 (IDXGISwapChain::Present, slot 8) at 0x1118B45.
constexpr std::size_t kRenderer=0x2136FF0,kSwapChain=0x90;
constexpr unsigned kPresentSlot=8;

constexpr double kLateMs=20.0,kSlowMs=33.4;
constexpr unsigned kSlowLinesMax=100;   // a period's SLOW FRAME lines at most (a 30 fps machine: every frame is slow)
constexpr unsigned kPasses=6;           // submits by pass 0..4, then every other pass

using InputFn=void(__fastcall*)(void*);
using UpdateFn=void(__fastcall*)(void*,float,std::uint32_t);
using DrawFn=void(__fastcall*)(void*,std::uint8_t,std::uint8_t);
using PassFn=void*(__fastcall*)(void*,void*,void*,void*);   // all four registers forwarded, rax back
using SubmitFn=void(__fastcall*)(void*,void*,int);
using MeshFn=void(__fastcall*)(void*,void*,int,std::uint8_t,void*);
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using QueryFn=bool(__fastcall*)(void*,void*,void*,void*,std::uint8_t);
using HavokStepFn=void(__fastcall*)(void*,void*,void*,float);
using HavokBuildFn=void(__fastcall*)(void*,void*,void*);
using HavokFinishFn=void(__fastcall*)(void*,void*);
using CollideDispatchFn=void(__fastcall*)(void*,void*,std::uint32_t);
using CollideSleepFn=DWORD(__fastcall*)(void*);

InputFn nextInput=nullptr;
UpdateFn nextUpdate=nullptr;
DrawFn nextDraw=nullptr;
PassFn nextResolve=nullptr,nextFence=nullptr,nextPresentCmd=nullptr;
SubmitFn nextSubmit[std::size(kSubmitCalls)]={};
MeshFn nextMesh[std::size(kMeshCalls)]={};
PresentFn nextPresent=nullptr;
QueryFn nextQuery=nullptr;
PassFn nextPalette=nullptr;
HavokStepFn nextHavokStep[2]{};
HavokBuildFn nextHavokBuild=nullptr;
HavokFinishFn nextHavokFinish=nullptr;
HavokBuildFn nextCollideConstruct=nullptr;
CollideSleepFn nextCollideSleep=nullptr;
bool split=false;          // PerfProbe: the frame split hooks are in
bool presentTried=false;
std::atomic<DWORD> gameThread{0};   // the main loop's thread (the frame anchor's), 0 before the first frame
bool anchored=false;                // the frame anchor chained
bool havokTiming=false,havokSplit=false,collideTiming=false,taskTrace=false,contactTrace=false;
unsigned contactImageSize=0;
thread_local bool profileCollide=false,profileCollideSleep=false,profileTaskWaits=false;
unsigned havokPhase=2;    // game thread only: 0 before objects, 1 after, 2 outside either tagged step

double tickMs=0.0;   // ms a QPC tick
LONGLONG Qpc() noexcept { LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart; }

struct HavokPart {
    unsigned calls=0;
    LONGLONG ticks=0,worst=0;
    void Add(LONGLONG elapsed) noexcept {
        ++calls;ticks+=elapsed;
        if(elapsed>worst)worst=elapsed;
    }
};
struct HavokGraphs { HavokPart build,finish; };
// The untagged bucket also keeps the total usable if either step call cannot be redirected.
HavokGraphs havokGraphs[3]{};
struct CollideTimes {
    HavokPart construct,dispatch,sleep;
    unsigned modeZero=0;
};
CollideTimes collideTimes;

struct TaskKind { unsigned vtable;const char* name; };
constexpr TaskKind kCollisionTasks[]={
    {0x18C1D90,"FinalizeNarrowPhaseGrid"},{0x18C1A78,"ProcessNarrowPhaseGrid"},
    {0x18C1AF8,"FinalizeNarrowPhasePairs"},{0x18C2158,"GenSolverTasks"},
    {0x18C2060,"CollideSync"},{0x18C1BF0,"SortFinalizeNarrowPhasePairs"},
    {0x18C4E98,"ProcessNarrowPhasePairs"},{0x18C5698,"ApplyDrag"},
    {0x18C5718,"PrepareNarrowPhase"},{0x18C57E8,"PrepareNarrowPhasePairs"},
    {0x18C59D8,"BuildConstraintJacobians"},{0x18C5968,"PrepareConstraints"},
    {0x18C1D28,"PostCollide"},
};
constexpr unsigned kGraphNodesMax=128,kGraphEdgesMax=512,kTaskSpansMax=1024,kWaitSpansMax=128,kGraphReportsMax=32;
struct GraphEdge { std::uint16_t from,to; };
static_assert(sizeof(GraphEdge)==4);
struct TaskSpan { LONGLONG begin,end;unsigned node;bool worker; };
struct WaitSpan { LONGLONG begin,end; };
constexpr unsigned kContactBuckets=64;
struct ContactBucket {
    unsigned type=0,callback=0,calls=0;
    LONGLONG elapsed=0;
};
struct ContactTrace {
    ContactBucket listeners[kContactBuckets]{};
    unsigned commands[256]{};
    unsigned count=0,dropped=0,dispatches=0;
    LONGLONG dispatch=0,signal=0;
};
thread_local ContactTrace* activeContact=nullptr;
thread_local bool inContactDispatch=false;
struct TaskNodeReport {
    const char* name=nullptr;
    LONGLONG begin=0,end=0,work=0,duringWait=0;
    unsigned calls=0,workers=0;
    int predecessor=-1;
};
struct GraphReport {
    TaskNodeReport nodes[kGraphNodesMax]{};
    ContactTrace contacts;
    unsigned id=0,count=0,concrete=0,observed=0,dropped=0;
    int tail=-1;
    LONGLONG origin=0,wait=0;
};
struct ActiveTaskGraph {
    const unsigned char* nativeNodes=nullptr;
    ContactTrace contacts;
    const void* tasks[kGraphNodesMax]{};
    const char* names[kGraphNodesMax]{};
    GraphEdge edges[kGraphEdgesMax]{};
    TaskSpan spans[kTaskSpansMax]{};
    WaitSpan waits[kWaitSpansMax]{};
    unsigned id=0,count=0,edgeCount=0,spanCount=0,waitCount=0,dropped=0;
    DWORD owner=0;
    LONGLONG origin=0;
};
SRWLOCK graphLock=SRWLOCK_INIT;
std::atomic<bool> graphActive{false};
struct TaskTraceStorage {
    ActiveTaskGraph graph;
    GraphReport reports[kGraphReportsMax];
};
TaskTraceStorage* taskTraceStorage=nullptr;
unsigned traceSteps=0,traceId=0,taskReportCount=0,taskTraceLost=0;
unsigned traceRejected=0;
int rejectedNodes=0,rejectedEdges=0;
const char* rejection=nullptr;

bool CaptureTaskGraph(const void* graph) noexcept {
    if(!taskTrace || (++traceSteps%16)!=0 || graphActive.load(std::memory_order_acquire))return false;
    ++traceRejected;
    rejection="unreadable";
    __try {
        const int count=At<int>(graph,kGraphNodeCount),edgeCount=At<int>(graph,kGraphEdgeCount);
        rejectedNodes=count;rejectedEdges=edgeCount;
        if(count<=0 || count>static_cast<int>(kGraphNodesMax) ||
           edgeCount<0 || edgeCount>static_cast<int>(kGraphEdgesMax)){rejection="size";return false;}
        const auto* nodes=At<const unsigned char*>(graph,kGraphNodes);
        const auto* edges=At<const GraphEdge*>(graph,kGraphEdges);
        if(!nodes || (edgeCount && !edges))return false;
        // The dispatcher has yet to publish these native nodes to workers.
        const void* tasks[kGraphNodesMax]{};
        const char* names[kGraphNodesMax]{};
        for(int i=0;i<count;++i) {
            const void* task=At<const void*>(nodes,static_cast<unsigned>(i)*kGraphNodeStride);
            tasks[i]=task;
            if(!task)continue;
            const void* table=At<const void*>(task,0);
            names[i]="Unknown";
            for(const auto& kind:kCollisionTasks)if(table==image+kind.vtable){names[i]=kind.name;break;}
        }
        GraphEdge snapshot[kGraphEdgesMax];
        unsigned validEdges=0,skippedEdges=0;
        for(int i=0;i<edgeCount;++i) {
            const auto& edge=edges[i];
            if(edge.from>=count || edge.to>=count){++skippedEdges;continue;}
            snapshot[validEdges++]=edge;
        }
        AcquireSRWLockExclusive(&graphLock);
        auto& taskGraph=taskTraceStorage->graph;
        taskGraph.id=++traceId;
        taskGraph.contacts={};
        taskGraph.count=static_cast<unsigned>(count);
        taskGraph.edgeCount=validEdges;
        taskGraph.nativeNodes=nodes;
        taskGraph.spanCount=taskGraph.waitCount=0;
        taskGraph.dropped=skippedEdges;
        taskGraph.owner=GetCurrentThreadId();
        taskGraph.origin=Qpc();
        std::memcpy(taskGraph.tasks,tasks,taskGraph.count*sizeof(tasks[0]));
        std::memcpy(taskGraph.names,names,taskGraph.count*sizeof(names[0]));
        std::memcpy(taskGraph.edges,snapshot,taskGraph.edgeCount*sizeof(snapshot[0]));
        graphActive.store(true,std::memory_order_release);
        ReleaseSRWLockExclusive(&graphLock);
        --traceRejected;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void FinishTaskGraph(bool completed) noexcept {
    AcquireSRWLockExclusive(&graphLock);
    graphActive.store(false,std::memory_order_release);
    ReleaseSRWLockExclusive(&graphLock);
    if(!completed)return;
    if(taskReportCount==kGraphReportsMax){++taskTraceLost;return;}
    const auto& taskGraph=taskTraceStorage->graph;
    auto& report=taskTraceStorage->reports[taskReportCount++];
    report={};
    report.id=taskGraph.id;
    report.count=taskGraph.count;
    report.origin=taskGraph.origin;
    report.contacts=taskGraph.contacts;
    report.dropped=taskGraph.dropped;
    for(unsigned i=0;i<taskGraph.count;++i) {
        report.nodes[i].name=taskGraph.names[i];
        if(taskGraph.tasks[i])++report.concrete;
    }
    for(unsigned i=0;i<taskGraph.waitCount;++i)report.wait+=taskGraph.waits[i].end-taskGraph.waits[i].begin;
    for(unsigned i=0;i<taskGraph.spanCount;++i) {
        const auto& span=taskGraph.spans[i];
        auto& node=report.nodes[span.node];
        if(!node.calls || span.begin<node.begin)node.begin=span.begin;
        if(span.end>node.end)node.end=span.end;
        ++node.calls;
        node.work+=span.end-span.begin;
        if(!span.worker)continue;
        ++node.workers;
        for(unsigned j=0;j<taskGraph.waitCount;++j) {
            const auto& wait=taskGraph.waits[j];
            const auto begin=span.begin>wait.begin ? span.begin : wait.begin;
            const auto end=span.end<wait.end ? span.end : wait.end;
            if(end>begin)node.duringWait+=end-begin;
        }
    }
    for(unsigned i=0;i<report.count;++i) {
        const auto& node=report.nodes[i];
        if(!node.calls)continue;
        ++report.observed;
        if(report.tail<0 || node.end>report.nodes[report.tail].end)report.tail=static_cast<int>(i);
    }
    for(unsigned i=0;i<taskGraph.edgeCount;++i) {
        const auto& edge=taskGraph.edges[i];
        const auto& from=report.nodes[edge.from];
        auto& to=report.nodes[edge.to];
        if(!from.calls || !to.calls || from.end>to.begin)continue;
        if(to.predecessor<0 || from.end>report.nodes[to.predecessor].end)to.predecessor=edge.from;
    }
}

using TaskExecuteFn=std::uintptr_t(__fastcall*)(void*,void*,void*,void*);

__declspec(noinline) std::uintptr_t CapturePostCollide(TaskExecuteFn original,unsigned id,
                                                      void* task,void* context,void* a3,void* a4) {
    ContactTrace sample{};
    auto* const previous=activeContact;
    activeContact=&sample;
    std::uintptr_t result=0;
    __try {
        result=original(task,context,a3,a4);
    } __finally {
        activeContact=previous;
    }
    AcquireSRWLockExclusive(&graphLock);
    auto& graph=taskTraceStorage->graph;
    if(graphActive.load(std::memory_order_relaxed) && id==graph.id)graph.contacts=sample;
    ReleaseSRWLockExclusive(&graphLock);
    return result;
}

std::uintptr_t __fastcall TaskExecuteProbe(void* task,void* context,void* a3,void* a4) {
    const auto original=At<TaskExecuteFn>(At<const void*>(task,0),0x10);
    unsigned id=0,node=0;
    DWORD owner=0;
    if(graphActive.load(std::memory_order_acquire)) {
        AcquireSRWLockShared(&graphLock);
        if(graphActive.load(std::memory_order_relaxed)) {
            const auto& taskGraph=taskTraceStorage->graph;
            // Mode 0 saves job+0x10 at context-0x18. The dispatcher writes the job into node+0x10.
            const auto* const jobData=At<const unsigned char*>(static_cast<const unsigned char*>(context)-0x18,0);
            const auto* const job=jobData-0x10;
            for(;node<taskGraph.count;++node) {
                if(taskGraph.tasks[node]!=task)continue;
                if(At<const void*>(taskGraph.nativeNodes,node*kGraphNodeStride+kGraphNodeJob)!=job)continue;
                id=taskGraph.id;
                owner=taskGraph.owner;
                break;
            }
        }
        ReleaseSRWLockShared(&graphLock);
    }
    if(!id)return original(task,context,a3,a4);
    const auto begin=Qpc();
    const auto result=contactTrace && At<const void*>(task,0)==image+kPostCollideVtable
        ? CapturePostCollide(original,id,task,context,a3,a4)
        : original(task,context,a3,a4);
    const auto end=Qpc();
    const bool worker=owner!=GetCurrentThreadId();
    AcquireSRWLockExclusive(&graphLock);
    auto& taskGraph=taskTraceStorage->graph;
    if(graphActive.load(std::memory_order_relaxed) && id==taskGraph.id) {
        if(taskGraph.spanCount<kTaskSpansMax)taskGraph.spans[taskGraph.spanCount++]={begin,end,node,worker};
        else ++taskGraph.dropped;
    }
    ReleaseSRWLockExclusive(&graphLock);
    return result;
}

void ReportTaskGraphs() noexcept {
    for(unsigned i=0;i<taskReportCount;++i) {
        const auto& graph=taskTraceStorage->reports[i];
        Log("PERF COLLIDE GRAPH %u nodes %u/%u dropped %u tail %d wait %.3f ms",
            graph.id,graph.observed,graph.concrete,graph.dropped,graph.tail,static_cast<double>(graph.wait)*tickMs);
        const auto& contact=graph.contacts;
        if(contact.dispatches) {
            Log("PERF COLLIDE CONTACT %u dispatches %u work %.3f ms signal %.3f ms dropped %u",
                graph.id,contact.dispatches,static_cast<double>(contact.dispatch)*tickMs,
                static_cast<double>(contact.signal)*tickMs,contact.dropped);
            for(unsigned j=0;j<std::size(contact.commands);++j)if(contact.commands[j])
                Log("PERF COLLIDE COMMAND %u kind %u calls %u",graph.id,j,contact.commands[j]);
            for(unsigned j=0;j<contact.count;++j) {
                const auto& listener=contact.listeners[j];
                Log("PERF COLLIDE LISTENER %u event %u callback 0x%X calls %u work %.3f ms",graph.id,
                    listener.type,listener.callback,listener.calls,static_cast<double>(listener.elapsed)*tickMs);
            }
        }
        for(unsigned j=0;j<graph.count;++j) {
            const auto& node=graph.nodes[j];
            if(!node.calls)continue;
            Log("PERF COLLIDE NODE %u index %u %s calls %u workers %u work %.3f ms withwait %.3f ms "
                "first %.3f ms last %.3f ms predecessor %d",graph.id,j,node.name,node.calls,node.workers,
                static_cast<double>(node.work)*tickMs,static_cast<double>(node.duringWait)*tickMs,
                static_cast<double>(node.begin-graph.origin)*tickMs,
                static_cast<double>(node.end-graph.origin)*tickMs,node.predecessor);
        }
    }
    if(taskTraceLost)Log("PERF COLLIDE GRAPH lost %u (report buffer full)",taskTraceLost);
    if(traceRejected)Log("PERF COLLIDE GRAPH skipped %u (last %s nodes %d edges %d)",
                         traceRejected,rejection,rejectedNodes,rejectedEdges);
    taskReportCount=taskTraceLost=traceRejected=0;
}

template<unsigned phase>
void __fastcall HavokStepHook(void* context,void* services,void* queue,float dt) {
    const unsigned previous=havokPhase;
    havokPhase=phase;
    __try {
        nextHavokStep[phase](context,services,queue,dt);
    } __finally {
        havokPhase=previous;
    }
}

void __fastcall HavokBuildHook(void* world,void* step,void* queue) {
    const LONGLONG start=Qpc();
    const bool previous=profileCollide;
    profileCollide=collideTiming && havokPhase==0;
    __try {
        nextHavokBuild(world,step,queue);
    } __finally {
        profileCollide=previous;
    }
    havokGraphs[havokPhase].build.Add(Qpc()-start);
}

void __fastcall HavokFinishHook(void* world,void* queue) {
    const LONGLONG start=Qpc();
    nextHavokFinish(world,queue);
    havokGraphs[havokPhase].finish.Add(Qpc()-start);
}

// Points the call at `rva` to `hook` over whatever it calls now: the stock function, or another hook's near thunk
// (common/memory.cpp AllocateNearThunk: mov rax,imm64; jmp rax) that ends in it. `*next` gets that target first.
bool Chain(unsigned rva,unsigned stock,void* hook,void** next) noexcept {
    __try {
        unsigned char* const site=image+rva;
        if(!Readable(site,5) || site[0]!=0xE8)return false;
        std::int32_t rel=0;std::memcpy(&rel,site+1,sizeof(rel));
        unsigned char* const now=site+5+rel;
        const bool thunk=now!=image+stock && Readable(now,12) && now[0]==0x48 && now[1]==0xB8 && now[10]==0xFF &&
                         now[11]==0xE0;
        if(now!=image+stock && !thunk)return false;
        *next=now;
        bool changed=false;
        // a failed write after the site changed keeps *next: the site calls the hook
        if(RedirectCall(site,now,hook,changed,edf::Claim::kLayered) || changed)return changed;
        *next=nullptr;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void __fastcall CollideConstructHook(void* world,void* step,void* graph) {
    if(!profileCollide) {
        nextCollideConstruct(world,step,graph);
        return;
    }
    const auto start=Qpc();
    nextCollideConstruct(world,step,graph);
    collideTimes.construct.Add(Qpc()-start);
}

void __fastcall CollideDispatchHook(void* queue,void* graph,std::uint32_t flags) {
    const auto table=At<const unsigned char*>(static_cast<const unsigned char*>(queue),0);
    const auto original=At<CollideDispatchFn>(table,0xA8);
    if(!profileCollide) {
        original(queue,graph,flags);
        return;
    }
    const bool previous=profileCollideSleep;
    // The audited sleep is specific to the default queue's mode-0 runner.
    profileCollideSleep=table==image+kDefaultQueueInterface &&
                        At<int>(static_cast<const unsigned char*>(queue),kDefaultQueueMode)==0;
    if(profileCollideSleep)++collideTimes.modeZero;
    const bool wasTracing=profileTaskWaits;
    const bool captured=profileCollideSleep && CaptureTaskGraph(graph);
    profileTaskWaits=captured;
    const auto start=Qpc();
    __try {
        original(queue,graph,flags);
    } __finally {
        profileCollideSleep=previous;
        profileTaskWaits=wasTracing;
        if(captured && AbnormalTermination())FinishTaskGraph(false);
    }
    collideTimes.dispatch.Add(Qpc()-start);
    if(captured)FinishTaskGraph(true);
}

DWORD __fastcall CollideSleepHook(void* event) {
    if(!profileCollideSleep)return nextCollideSleep(event);
    const auto start=Qpc();
    const DWORD result=nextCollideSleep(event);
    const auto end=Qpc();
    collideTimes.sleep.Add(end-start);
    if(profileTaskWaits) {
        auto& taskGraph=taskTraceStorage->graph;
        if(taskGraph.waitCount<kWaitSpansMax)taskGraph.waits[taskGraph.waitCount++]={start,end};
        else {
            AcquireSRWLockExclusive(&graphLock);
            ++taskGraph.dropped;
            ReleaseSRWLockExclusive(&graphLock);
        }
    }
    return result;
}

template<std::size_t N>
bool InstallIndirectProbe(unsigned call,const unsigned char (&signature)[N],void* hook) noexcept {
    constexpr auto argumentBytes=N-6;
    const auto at=image+call;
    if(!edf::Matches(image,call-argumentBytes,signature,N))return false;
    auto* const thunk=static_cast<unsigned char*>(edf::AllocateNearThunk(at,hook));
    if(!thunk)return false;
    // Preserve the six-byte virtual call's footprint.
    unsigned char code[]={0xE8,0,0,0,0,0x90};
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(at+5);
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto rel32=static_cast<std::int32_t>(delta);
        std::memcpy(code+1,&rel32,sizeof(rel32));
        if(edf::PatchCode(at,signature+argumentBytes,code,sizeof(code)))return true;
    }
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}

using DispatchContactsFn=std::uintptr_t(__fastcall*)(void*,void*,void*,void*);
using ContactSignalFn=std::uintptr_t(__fastcall*)(void*,void*);
DispatchContactsFn nextContactDispatch=nullptr;
ContactSignalFn nextContactSignal=nullptr,nextEventCommand=nullptr;

std::uintptr_t __fastcall ContactDispatchProbe(void* store,void* stream,void* dispatcher,void* extra) {
    auto* const trace=activeContact;
    if(!trace)return nextContactDispatch(store,stream,dispatcher,extra);
    const auto begin=Qpc();
    const bool previous=inContactDispatch;
    inContactDispatch=true;
    std::uintptr_t result=0;
    __try {
        result=nextContactDispatch(store,stream,dispatcher,extra);
    } __finally {
        inContactDispatch=previous;
    }
    ++trace->dispatches;
    trace->dispatch+=Qpc()-begin;
    return result;
}

std::uintptr_t __fastcall PostCollideSignalProbe(void* signal,void* world) {
    auto* const trace=activeContact;
    if(!trace)return nextContactSignal(signal,world);
    const auto begin=Qpc();
    const auto result=nextContactSignal(signal,world);
    trace->signal+=Qpc()-begin;
    return result;
}

std::uintptr_t __fastcall EventCommandProbe(void* dispatcher,void* command) {
    if(activeContact && inContactDispatch)++activeContact->commands[At<unsigned char>(command,3)];
    return nextEventCommand(dispatcher,command);
}

std::uintptr_t __fastcall EventListenerProbe(void* listener,void* input,void* event,const void* table) {
    const auto original=At<DispatchContactsFn>(table,0x18);
    auto* const trace=activeContact;
    if(!trace || !inContactDispatch)return original(listener,input,event,const_cast<void*>(table));
    const unsigned type=At<std::uint16_t>(event,4);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto address=reinterpret_cast<std::uintptr_t>(original)-base;
    // Common MemberSlot adapter: use the actual listener when its instructions prove the slot layout.
    constexpr unsigned char adapter[]={0x48,0x8B,0xC1,0x48,0x8B,0x49,0x10,0x48,0xFF,0x60,0x18};
    if(contactImageSize>=sizeof(adapter) && address<=contactImageSize-sizeof(adapter) &&
       edf::Matches(image,address,adapter,sizeof(adapter)))address=At<std::uintptr_t>(listener,0x18)-base;
    const auto callback=address<contactImageSize ? static_cast<unsigned>(address) : 0u;
    unsigned slot=0;
    for(;slot<trace->count;++slot) {
        const auto& item=trace->listeners[slot];
        if(item.type==type && item.callback==callback)break;
    }
    if(slot==kContactBuckets) {
        ++trace->dropped;
        return original(listener,input,event,const_cast<void*>(table));
    }
    auto& item=trace->listeners[slot];
    if(slot==trace->count){item.type=type;item.callback=callback;++trace->count;}
    ++item.calls;
    const auto begin=Qpc();
    const auto result=original(listener,input,event,const_cast<void*>(table));
    item.elapsed+=Qpc()-begin;
    return result;
}

bool InstallEventListenerProbe() noexcept {
    if(!edf::Matches(image,kEventListenerCall,kEventListenerSig,sizeof(kEventListenerSig)))return false;
    auto* const site=image+kEventListenerCall;
    // Recreate mov r8,rbp and mov rdx,r15, then tail-jump to the diagnostic wrapper.
    unsigned char bridge[]={0x4C,0x8B,0xC5,0x49,0x8B,0xD7,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto destination=reinterpret_cast<std::uintptr_t>(&EventListenerProbe);
    std::memcpy(bridge+8,&destination,sizeof(destination));
    auto* const relay=static_cast<unsigned char*>(edf::AllocateNearCode(site,bridge,sizeof(bridge)));
    if(!relay)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(site+5);
    unsigned char code[]={0xE8,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
    static_assert(sizeof(code)==sizeof(kEventListenerSig));
    if(delta>=INT32_MIN && delta<=INT32_MAX) {
        const auto rel=static_cast<std::int32_t>(delta);
        std::memcpy(code+1,&rel,sizeof(rel));
        if(edf::PatchCode(site,kEventListenerSig,code,sizeof(code)))return true;
    }
    VirtualFree(relay,0,MEM_RELEASE);
    return false;
}

void InstallContactTrace() noexcept {
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+At<LONG>(image,0x3C));
    contactImageSize=pe->OptionalHeader.SizeOfImage;
    const bool dispatch=Chain(kContactDispatchCall,kContactDispatch,reinterpret_cast<void*>(&ContactDispatchProbe),
                              reinterpret_cast<void**>(&nextContactDispatch));
    const bool signal=Chain(kPostCollideSignalCall,kPostCollideSignal,reinterpret_cast<void*>(&PostCollideSignalProbe),
                            reinterpret_cast<void**>(&nextContactSignal));
    const bool command=Chain(kEventCommandCall,kEventCommand,reinterpret_cast<void*>(&EventCommandProbe),
                             reinterpret_cast<void**>(&nextEventCommand));
    const bool listener=InstallEventListenerProbe();
    contactTrace=dispatch && signal && command && listener;
    Log("HOOK perf PostCollide dispatch=%d signal=%d command=%d listener=%d",dispatch,signal,command,listener);
}

void InstallCollideProbe() noexcept {
    const bool construct=Chain(kCollideConstructCall,kCollideConstruct,reinterpret_cast<void*>(&CollideConstructHook),
                              reinterpret_cast<void**>(&nextCollideConstruct));
    const bool sleep=Chain(kCollideSleepCall,kCollideSleep,reinterpret_cast<void*>(&CollideSleepHook),
                          reinterpret_cast<void**>(&nextCollideSleep));
    unsigned dispatch=0;
    for(const auto call:kCollideDispatchCalls)
        dispatch+=InstallIndirectProbe(call,kCollideDispatchSig,reinterpret_cast<void*>(&CollideDispatchHook));
    collideTiming=construct && sleep && dispatch==std::size(kCollideDispatchCalls);
    Log("HOOK perf Havok collide construct=%d dispatch=%u/%zu sleep=%d",construct,dispatch,std::size(kCollideDispatchCalls),sleep);
    if(collideTiming && Cfg().perfProbe>=2) {
        taskTraceStorage=new(std::nothrow) TaskTraceStorage{};
        taskTrace=taskTraceStorage &&
            InstallIndirectProbe(kModeZeroExecute,kTaskExecuteSig,reinterpret_cast<void*>(&TaskExecuteProbe));
        if(!taskTrace){delete taskTraceStorage;taskTraceStorage=nullptr;}
        Log("HOOK perf Havok collision tasks=%d (one graph sampled every 16 builds)",taskTrace);
        if(taskTrace)InstallContactTrace();
    }
}

// --- The frame (game thread only) ---
struct Frame { LONGLONG begin,inputEnd,update,draw,resolve,fenceBegin,fence,present; const void* scene; std::uint32_t step; bool open; };
Frame cur{};
struct Base { unsigned frames,steps,over20,over33,slowLines,slowDropped; double sum,worst; } base{};
struct Split { unsigned frames,noStep; double input,app,draw,resolve,steps,stepCount,fence,present,tail,without; } parts{};
// the input samples' QPC of the last three frames (the Present pairs with the one its scene was stepped with)
LONGLONG inputs[3]={};

// The render thread's Present pairs with the frame whose Present command was queued: a single-producer ring.
struct Queued { LONGLONG own,stepped; };
constexpr unsigned kRing=64;
Queued ring[kRing];
std::atomic<unsigned> ringHead{0},ringTail{0};

// What the render thread and the draw submits count (whichever thread they run on): relaxed atomics, taken each period.
std::atomic<unsigned> presents{0},unpaired{0};
std::atomic<long long> presentTicks{0},presentWorst{0},gapTicks{0},gapWorst{0},ownTicks{0},steppedTicks{0},steppedWorst{0};
std::atomic<unsigned> paired{0};
LONGLONG lastPresent=0;   // render thread only
std::atomic<unsigned> submits[kPasses]={},meshes{0};
// The occlusion query reads (calls, not in yet, the waiting ones and their time) and the skin palettes (calls, time).
std::atomic<unsigned> queries{0},queriesPending{0},queryWaits{0},palettes{0};
std::atomic<long long> queryTicks{0},queryWaitTicks{0},queryWorst{0},paletteTicks{0};

void Max(std::atomic<long long>& m,long long v) noexcept {
    long long was=m.load(std::memory_order_relaxed);
    while(v>was && !m.compare_exchange_weak(was,v,std::memory_order_relaxed)){}
}

void CloseFrame(LONGLONG now) noexcept {
    const double ms=static_cast<double>(now-cur.begin)*tickMs;
    const SceneStep s=SceneStepNow();
    const std::uint32_t steps=s.scene && s.scene==cur.scene ? s.count-cur.step : 0u;
    if(ms<1000.0) {
        ++base.frames;base.steps+=steps;base.sum+=ms;base.over20+=ms>kLateMs;base.over33+=ms>kSlowMs;
        if(ms>base.worst)base.worst=ms;
    }
    const bool whole=split && cur.fenceBegin && cur.update;
    double app=0.0,stepMs=0.0,tail=0.0,fence=0.0,draw=0.0;
    if(whole) {
        draw=static_cast<double>(cur.draw)*tickMs;
        stepMs=static_cast<double>(cur.update-cur.draw)*tickMs;
        app=static_cast<double>(cur.fenceBegin-cur.inputEnd-cur.update)*tickMs;
        fence=static_cast<double>(cur.fence)*tickMs;
        const double input=static_cast<double>(cur.inputEnd-cur.begin)*tickMs,present=static_cast<double>(cur.present)*tickMs;
        tail=static_cast<double>(now-cur.fenceBegin-cur.fence-cur.present)*tickMs;
        if(ms<1000.0) {
            ++parts.frames;parts.noStep+=steps==0;parts.input+=input;parts.app+=app;parts.draw+=draw;
            parts.resolve+=static_cast<double>(cur.resolve)*tickMs;parts.steps+=stepMs;parts.stepCount+=steps;
            parts.fence+=fence;parts.present+=present;parts.tail+=tail;parts.without+=input+app+draw+fence+present;
        }
    }
    if(ms>kSlowMs && Cfg().debug) {
        if(base.slowLines>=kSlowLinesMax)++base.slowDropped;
        else if(whole) {
            ++base.slowLines;
            Log("SLOW FRAME %lld %lld %.1f ms, %u steps: app %.1f, draw %.1f, steps %.1f, fence %.1f, tail %.1f",cur.begin,now,
                ms,steps,app,draw,stepMs,fence,tail);
        } else {
            ++base.slowLines;
            Log("SLOW FRAME %lld %lld %.1f ms, %u steps",cur.begin,now,ms,steps);
        }
    }
}

void TryPresentHook() noexcept;

// DevFrameCap (with Debug): the main loop held to at most this many frames a second at its input sample (once a
// frame, before the frame's steps: the game's fixed step then runs two steps a frame at 30, the game's speed kept; a
// per-step wait would slow the steps and the loop would make up with more of them), a high resolution waitable timer,
// so a run that is not about the frame rate does not drive the GPU flat out.
void CapFrameRate(int cap) noexcept {
    static HANDLE timer=nullptr;
    static LARGE_INTEGER last{},freq{};
    if(cap<=0){last.QuadPart=0;return;}
    if(!freq.QuadPart)QueryPerformanceFrequency(&freq);
    if(!timer)timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    const LONGLONG period=freq.QuadPart/cap;
    if(last.QuadPart && now.QuadPart-last.QuadPart<period) {
        const LONGLONG wait=period-(now.QuadPart-last.QuadPart);
        LARGE_INTEGER due;due.QuadPart=-(wait*10000000LL/freq.QuadPart);   // 100 ns units, relative
        if(timer && due.QuadPart<0 && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,100);
        QueryPerformanceCounter(&now);
    }
    last=now;
}

void __fastcall InputHook(void* input) {
    CapFrameRate(Cfg().devFrameCap);
    const LONGLONG now=Qpc();
    gameThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    if(cur.open)CloseFrame(now);
    const SceneStep s=SceneStepNow();
    cur=Frame{};cur.begin=now;cur.scene=s.scene;cur.step=s.count;cur.open=true;
    inputs[2]=inputs[1];inputs[1]=inputs[0];inputs[0]=now;
    nextInput(input);
    cur.inputEnd=Qpc();
    if(split && !presentTried)TryPresentHook();
}
void __fastcall UpdateHook(void* system,float dt,std::uint32_t steps) {
    const LONGLONG t=Qpc();
    nextUpdate(system,dt,steps);
    cur.update+=Qpc()-t;
}
void __fastcall DrawHook(void* system,std::uint8_t a,std::uint8_t b) {
    const LONGLONG t=Qpc();
    nextDraw(system,a,b);
    cur.draw+=Qpc()-t;
}
void* __fastcall ResolveHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    void* const r=nextResolve(a,b,c,d);
    cur.resolve+=Qpc()-t;
    return r;
}
void* __fastcall FenceHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    void* const r=nextFence(a,b,c,d);
    if(!cur.fenceBegin)cur.fenceBegin=t;
    cur.fence+=Qpc()-t;
    return r;
}
void* __fastcall PresentCmdHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    // queued before the command: the render thread may run it at once
    const unsigned head=ringHead.load(std::memory_order_relaxed);
    if(head-ringTail.load(std::memory_order_acquire)<kRing) {
        ring[head%kRing]=Queued{inputs[0],inputs[2]};
        ringHead.store(head+1,std::memory_order_release);
    }
    void* const r=nextPresentCmd(a,b,c,d);
    cur.present+=Qpc()-t;
    return r;
}
void __fastcall SubmitHook(unsigned i,void* system,void* object,int pass) {
    submits[pass>=0 && pass<static_cast<int>(kPasses)-1 ? pass : kPasses-1].fetch_add(1,std::memory_order_relaxed);
    nextSubmit[i](system,object,pass);
}
template<unsigned I> void __fastcall SubmitAt(void* system,void* object,int pass) { SubmitHook(I,system,object,pass); }
template<unsigned I> void __fastcall MeshAt(void* mesh,void* ctx,int pass,std::uint8_t flag,void* cache) {
    meshes.fetch_add(1,std::memory_order_relaxed);
    nextMesh[I](mesh,ctx,pass,flag,cache);
}

bool __fastcall QueryHook(void* renderer,void* query,void* c,void* result,std::uint8_t wait) {
    const LONGLONG t=Qpc();
    const bool in=nextQuery(renderer,query,c,result,wait);
    const LONGLONG d=Qpc()-t;
    queries.fetch_add(1,std::memory_order_relaxed);
    if(!in)queriesPending.fetch_add(1,std::memory_order_relaxed);
    queryTicks.fetch_add(d,std::memory_order_relaxed);Max(queryWorst,d);
    if(wait){queryWaits.fetch_add(1,std::memory_order_relaxed);queryWaitTicks.fetch_add(d,std::memory_order_relaxed);}
    return in;
}
void* __fastcall PaletteHook(void* a,void* b,void* c,void* d) {
    const LONGLONG t=Qpc();
    void* const r=nextPalette(a,b,c,d);
    paletteTicks.fetch_add(Qpc()-t,std::memory_order_relaxed);
    palettes.fetch_add(1,std::memory_order_relaxed);
    return r;
}

// --- The render thread's Present ---
HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain,UINT sync,UINT flags) {
    const LONGLONG t=Qpc();
    const HRESULT hr=nextPresent(chain,sync,flags);
    const LONGLONG done=Qpc();
    presents.fetch_add(1,std::memory_order_relaxed);
    presentTicks.fetch_add(done-t,std::memory_order_relaxed);Max(presentWorst,done-t);
    if(lastPresent){gapTicks.fetch_add(done-lastPresent,std::memory_order_relaxed);Max(gapWorst,done-lastPresent);}
    lastPresent=done;
    const unsigned tail=ringTail.load(std::memory_order_relaxed);
    if(tail!=ringHead.load(std::memory_order_acquire)) {
        const Queued q=ring[tail%kRing];
        ringTail.store(tail+1,std::memory_order_release);
        if(q.own && q.stepped) {
            paired.fetch_add(1,std::memory_order_relaxed);
            ownTicks.fetch_add(done-q.own,std::memory_order_relaxed);
            steppedTicks.fetch_add(done-q.stepped,std::memory_order_relaxed);Max(steppedWorst,done-q.stepped);
        }
    } else unpaired.fetch_add(1,std::memory_order_relaxed);
    return hr;
}

// The swap chain as the renderer holds it, checked to be one (its own QueryInterface), or null.
IDXGISwapChain* SwapChain() noexcept {
    __try {
        void* const p=At<void*>(image,kRenderer+kSwapChain);
        if(!p || !Readable(p,8))return nullptr;
        void** const vt=*static_cast<void***>(p);
        if(!Readable(vt,(kPresentSlot+1)*sizeof(void*)))return nullptr;
        void* q=nullptr;
        if(FAILED(static_cast<IUnknown*>(p)->QueryInterface(__uuidof(IDXGISwapChain),&q)) || !q)return nullptr;
        static_cast<IUnknown*>(q)->Release();
        return q==p ? static_cast<IDXGISwapChain*>(p) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

// Once, with the swap chain made: the adapter and the swap chain as created (gpu-2), and the Present hooked.
void TryPresentHook() noexcept {
    IDXGISwapChain* const chain=SwapChain();
    if(!chain)return;
    presentTried=true;
    DXGI_SWAP_CHAIN_DESC d{};
    if(SUCCEEDED(chain->GetDesc(&d)))
        Log("PERF RENDER swap chain %ux%u %u/%u Hz, %u buffer(s), swap effect %d, flags 0x%X, windowed %d",d.BufferDesc.Width,
            d.BufferDesc.Height,d.BufferDesc.RefreshRate.Numerator,d.BufferDesc.RefreshRate.Denominator,d.BufferCount,
            static_cast<int>(d.SwapEffect),d.Flags,static_cast<int>(d.Windowed));
    IDXGIDevice* device=nullptr;
    if(SUCCEEDED(chain->GetDevice(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&device))) && device) {
        IDXGIAdapter* adapter=nullptr;
        DXGI_ADAPTER_DESC a{};
        if(SUCCEEDED(device->GetAdapter(&adapter)) && adapter && SUCCEEDED(adapter->GetDesc(&a)))
            Log("PERF RENDER adapter %ls, %llu MB video memory",a.Description,
                static_cast<unsigned long long>(a.DedicatedVideoMemory>>20));
        if(adapter)adapter->Release();
        device->Release();
    }
    void** const slot=*reinterpret_cast<void***>(chain)+kPresentSlot;
    // nextPresent is written before the slot: the render thread may present at once
    const bool ok=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&PresentHook),reinterpret_cast<void**>(&nextPresent));
    Log("HOOK perf present=%d (the render thread's Present, input to Present)",ok);
}

// "<key> <msgs>/<bytes> B" for each row used, zeroed (the keys stay: a channel keeps its row).
int NetRows(netcount::Row* rows,const char* keyFormat,char* out,std::size_t size) noexcept {
    int n=0;
    for(unsigned i=0;i<netcount::kRows;++i) {
        const unsigned long long k=rows[i].key.load(std::memory_order_relaxed);
        const unsigned m=rows[i].msgs.exchange(0,std::memory_order_relaxed);
        const unsigned long long b=rows[i].bytes.exchange(0,std::memory_order_relaxed);
        if(!k || !m || static_cast<std::size_t>(n)>=size)continue;
        char key[24];std::snprintf(key,sizeof(key),keyFormat,static_cast<unsigned>(k-1));
        const int w=std::snprintf(out+n,size-n,"%s%s %u/%llu B",n ? ", " : "",key,m,b);
        if(w>0)n+=w;
    }
    return n;
}
}  // namespace

void Install() noexcept {
    LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);
    tickMs=1000.0/static_cast<double>(hz.QuadPart);
    if(!Cfg().debug)return;
    // each hook's next is the global it calls through, written by Chain before the site is
    const bool anchor=Chain(kInputCall,kInput,reinterpret_cast<void*>(&InputHook),reinterpret_cast<void**>(&nextInput));
    anchored=anchor;
    Log("PERF QPC %lld Hz; HOOK perf frame=%d (the main loop's input sample: frame times, steps a frame, SLOW FRAME)",
        hz.QuadPart,anchor);
    if(!anchor || !Cfg().perfProbe)return;
    // The frame split: all or none (a frame's parts must add up).
    const bool whole=Chain(kUpdateCall,kUpdate,reinterpret_cast<void*>(&UpdateHook),reinterpret_cast<void**>(&nextUpdate)) &&
        Chain(kDrawCall,kDraw,reinterpret_cast<void*>(&DrawHook),reinterpret_cast<void**>(&nextDraw)) &&
        Chain(kResolveCall,kResolve,reinterpret_cast<void*>(&ResolveHook),reinterpret_cast<void**>(&nextResolve)) &&
        Chain(kFenceCall,kFence,reinterpret_cast<void*>(&FenceHook),reinterpret_cast<void**>(&nextFence)) &&
        Chain(kPresentCall,kPresentCmd,reinterpret_cast<void*>(&PresentCmdHook),reinterpret_cast<void**>(&nextPresentCmd));
    split=whole;
    // a site chained before one failed stays chained: its hook only times and calls through
    void* const submitHooks[]={reinterpret_cast<void*>(&SubmitAt<0>),reinterpret_cast<void*>(&SubmitAt<1>),
                               reinterpret_cast<void*>(&SubmitAt<2>),reinterpret_cast<void*>(&SubmitAt<3>),
                               reinterpret_cast<void*>(&SubmitAt<4>)};
    static_assert(std::size(submitHooks)==std::size(kSubmitCalls));
    unsigned subs=0,mesh=0;
    for(unsigned i=0;i<std::size(kSubmitCalls);++i) {
        subs+=Chain(kSubmitCalls[i],kSubmit,submitHooks[i],reinterpret_cast<void**>(&nextSubmit[i]));
    }
    void* const meshHooks[]={reinterpret_cast<void*>(&MeshAt<0>),reinterpret_cast<void*>(&MeshAt<1>)};
    static_assert(std::size(meshHooks)==std::size(kMeshCalls));
    for(unsigned i=0;i<std::size(kMeshCalls);++i) {
        mesh+=Chain(kMeshCalls[i],kMesh,meshHooks[i],reinterpret_cast<void**>(&nextMesh[i]));
    }
    Log("HOOK perf split=%d submits=%u/%zu meshes=%u/%zu (PerfProbe=1: the frame's parts, the draw counts)",whole,subs,
        std::size(kSubmitCalls),mesh,std::size(kMeshCalls));
    const bool query=Chain(kQueryCall,kQuery,reinterpret_cast<void*>(&QueryHook),reinterpret_cast<void**>(&nextQuery));
    const bool palette=Chain(kPaletteCall,kPalette,reinterpret_cast<void*>(&PaletteHook),reinterpret_cast<void**>(&nextPalette));
    Log("HOOK perf queries=%d palettes=%d (PerfProbe=1: the occlusion query reads, the skin palettes)",query,palette);
    const bool build=Chain(kHavokBuildCall,kHavokBuild,reinterpret_cast<void*>(&HavokBuildHook),reinterpret_cast<void**>(&nextHavokBuild));
    const bool finish=Chain(kHavokFinishCall,kHavokFinish,reinterpret_cast<void*>(&HavokFinishHook),reinterpret_cast<void**>(&nextHavokFinish));
    havokTiming=build && finish;
    if(havokTiming) {
        const bool before=Chain(kHavokBeforeCall,kHavokStep,reinterpret_cast<void*>(&HavokStepHook<0>),
                                reinterpret_cast<void**>(&nextHavokStep[0]));
        const bool after=Chain(kHavokAfterCall,kHavokStep,reinterpret_cast<void*>(&HavokStepHook<1>),
                               reinterpret_cast<void**>(&nextHavokStep[1]));
        havokSplit=before && after;
        if(havokSplit)InstallCollideProbe();
        Log("HOOK perf Havok build=%d finish=%d before=%d after=%d",build,finish,before,after);
    } else {
        Log("HOOK perf Havok build=%d finish=%d",build,finish);
    }
}

bool ChainCall(unsigned rva,unsigned stock,void* hook,void** next) noexcept { return Chain(rva,stock,hook,next); }
DWORD GameThread() noexcept { return gameThread.load(std::memory_order_relaxed); }
bool FrameAnchored() noexcept { return anchored; }

Frames TakeFrames() noexcept {
    const Frames f{base.frames,base.steps,base.over20,base.over33,base.sum,base.worst};
    if(base.slowDropped)Log("SLOW FRAME %u more not logged (at most %u lines a period)",base.slowDropped,kSlowLinesMax);
    base=Base{};
    return f;
}

void Report(unsigned frames) noexcept {
    if(Cfg().debug && split && parts.frames) {
        const double n=parts.frames;
        Log("PERF FRAME %u frames split, %u without a step: a frame input %.2f, app %.2f, draw %.2f (umbra resolve %.2f), "
            "steps %.2f (%.2f a step), fence wait %.2f, present command %.3f, tail %.2f ms (the tick wait in it); "
            "without the steps and the tail %.2f ms",parts.frames,parts.noStep,parts.input/n,parts.app/n,parts.draw/n,
            parts.resolve/n,parts.steps/n,parts.stepCount>0 ? parts.steps/parts.stepCount : 0.0,parts.fence/n,
            parts.present/n,parts.tail/n,parts.without/n);
    }
    parts=Split{};
    if(havokTiming) {
        const auto report=[](const char* phase,const HavokGraphs& graphs) noexcept {
            if(!graphs.build.calls || !graphs.finish.calls)return;
            Log("PERF HAVOK %sbuild %u calls %.3f ms total max %.3f ms, finish %u calls %.3f ms total max %.3f ms",
                phase,graphs.build.calls,static_cast<double>(graphs.build.ticks)*tickMs,
                static_cast<double>(graphs.build.worst)*tickMs,graphs.finish.calls,
                static_cast<double>(graphs.finish.ticks)*tickMs,static_cast<double>(graphs.finish.worst)*tickMs);
        };
        auto total=havokGraphs[2];
        for(unsigned i=0;i<2;++i) {
            const auto& graphs=havokGraphs[i];
            total.build.calls+=graphs.build.calls;total.build.ticks+=graphs.build.ticks;
            if(graphs.build.worst>total.build.worst)total.build.worst=graphs.build.worst;
            total.finish.calls+=graphs.finish.calls;total.finish.ticks+=graphs.finish.ticks;
            if(graphs.finish.worst>total.finish.worst)total.finish.worst=graphs.finish.worst;
        }
        report("",total);
        if(havokSplit) {
            report("before ",havokGraphs[0]);
            report("after ",havokGraphs[1]);
        }
    }
    if(collideTiming && collideTimes.construct.calls) {
        const auto& c=collideTimes;
        Log("PERF COLLIDE construct %u calls %.3f ms, dispatch %u calls %.3f ms, mode0 %u, sleep %u calls %.3f ms",
            c.construct.calls,static_cast<double>(c.construct.ticks)*tickMs,c.dispatch.calls,
            static_cast<double>(c.dispatch.ticks)*tickMs,c.modeZero,c.sleep.calls,static_cast<double>(c.sleep.ticks)*tickMs);
        collideTimes={};
    }
    if(taskTrace)ReportTaskGraphs();
    const auto ground=TakeGroundProbeStats();
    if(Cfg().perfProbe && ground.floor)
        Log("PERF NAV RAYS %u batches, %u floor, %u clearance, %u water, %u dry",
            ground.batches,ground.floor,ground.clearance,ground.water,ground.dryWater);
    if(Cfg().perfProbe && ground.bodyListQueries)
        Log("PERF NAV BODY RAYS views=%u tested=%u clear=%u floor=%u queries=%u reused=%u",
            ground.bodyLists,ground.bodyRays,ground.bodyClear,ground.bodyFloor,ground.bodyListQueries,ground.bodyListReused);
    for(auto& graphs:havokGraphs)graphs={};
    const unsigned p=presents.exchange(0,std::memory_order_relaxed),u=unpaired.exchange(0,std::memory_order_relaxed);
    const unsigned pr=paired.exchange(0,std::memory_order_relaxed);
    const double pt=static_cast<double>(presentTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double pw=static_cast<double>(presentWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    const double gt=static_cast<double>(gapTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double gw=static_cast<double>(gapWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    const double ot=static_cast<double>(ownTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double st=static_cast<double>(steppedTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double sw=static_cast<double>(steppedWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    unsigned sub[kPasses];
    for(unsigned i=0;i<kPasses;++i)sub[i]=submits[i].exchange(0,std::memory_order_relaxed);
    const unsigned me=meshes.exchange(0,std::memory_order_relaxed);
    if(Cfg().debug && p) {
        const double f=frames ? frames : 1.0;
        Log("PERF RENDER %u presents: the call %.2f ms (worst %.2f), %.2f ms apart (worst %.2f); input to present %.1f ms "
            "(the frame's own sample), %.1f ms (worst %.1f; the sample its scene was stepped with, two frames back), %u "
            "unpaired; submits a frame by pass %.0f %.0f %.0f %.0f %.0f, other %.0f; mesh draws a frame %.0f",p,pt/p,pw,
            p>1 ? gt/(p-1) : 0.0,gw,pr ? ot/pr : 0.0,pr ? st/pr : 0.0,sw,u,sub[0]/f,sub[1]/f,sub[2]/f,sub[3]/f,sub[4]/f,
            sub[5]/f,me/f);
    }
    const unsigned q=queries.exchange(0,std::memory_order_relaxed),qp=queriesPending.exchange(0,std::memory_order_relaxed);
    const unsigned qw=queryWaits.exchange(0,std::memory_order_relaxed),pa=palettes.exchange(0,std::memory_order_relaxed);
    const double qt=static_cast<double>(queryTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double qwt=static_cast<double>(queryWaitTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    const double qx=static_cast<double>(queryWorst.exchange(0,std::memory_order_relaxed))*tickMs;
    const double pat=static_cast<double>(paletteTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    if(Cfg().debug && (q || pa)) {
        const double f=frames ? frames : 1.0;
        Log("PERF GPU a frame: occlusion query reads %.1f (%.1f not in yet, %.1f waiting), %.3f ms (waiting %.3f, worst "
            "call %.2f); skin palettes %.1f, %.3f ms",q/f,qp/f,qw/f,qt/f,qwt/f,qx,pa/f,pat/f);
    }
    char out[700],in[700];
    const int no=NetRows(netcount::out,"head 0x%08X",out,sizeof(out)),ni=NetRows(netcount::in,"head 0x%08X",in,sizeof(in));
    const unsigned w=netcount::walks.exchange(0,std::memory_order_relaxed);
    const double wt=static_cast<double>(netcount::walkTicks.exchange(0,std::memory_order_relaxed))*tickMs;
    if(Cfg().debug && (no || ni || w))
        Log("PERF NET CHANNELS out: %s; in: %s; world walks %u (%.2f ms, %.2f a frame)",no ? out : "none",ni ? in : "none",w,
            wt,frames ? static_cast<double>(w)/frames : 0.0);
}

}  // namespace crew::perfprobe

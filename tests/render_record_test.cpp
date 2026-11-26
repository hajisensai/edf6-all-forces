// The render recording's single owner (src/render_record.cpp): the call 0x1198FA4 checked and redirected once however
// many features install, the features' turns run in Slot order (the frame rate's poses, then the first person's boxes)
// before the game's recording with its arguments, the projection turns at each projection with the recording's first
// marked once (none for a projection outside it or after a recording that never projected), and each feature's turn runs without the other registered.
#include "../src/render_record.cpp"
#include <cstdio>

namespace {
int matches=0,redirects=0;
bool sigOk=true,redirectOk=true;
void* redirectedTo=nullptr;
bool shareOk=true;
int shares=0;
void (*projectHook)(void*,const int*)=nullptr;
}  // namespace

namespace crew::scopelens {
bool SetBeforeProject(BeforeProjectFn fn) noexcept {
    ++shares;
    if(!shareOk)return false;
    projectHook=fn;
    return true;
}
}  // namespace crew::scopelens

namespace edf {
bool Matches(const unsigned char*,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    ++matches;
    return sigOk && rva==crew::kRecordCall-14 && size==19 && bytes[size-5]==0xE8;
}
bool RedirectCall(unsigned char* site,void* expected,void* replacement,bool& changed,edf::Claim,const char*) noexcept {
    ++redirects;
    if(!redirectOk || site!=crew::image+crew::kRecordCall || expected!=crew::image+crew::kRecordDraw)return false;
    redirectedTo=replacement;
    changed=true;
    return true;
}
}  // namespace edf

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}  // namespace crew

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

char order[8]{};
int orderAt=0;
std::uint64_t recordArgs[2]{};
void* recordSys=nullptr;
int recordCalls=0;
void FrameRateTurn() noexcept { if(orderAt<7)order[orderAt++]='R'; }
void FirstPersonTurn() noexcept { if(orderAt<7)order[orderAt++]='F'; }
// The projection turns: 'P' / 'Q' the frame rate's / first person's at a first projection, 'p' / 'q' at any other.
void FrameRateProject(void*,const int*,bool first) noexcept { if(orderAt<7)order[orderAt++]=first ? 'P' : 'p'; }
void FirstPersonProject(void*,const int*,bool first) noexcept { if(orderAt<7)order[orderAt++]=first ? 'Q' : 'q'; }
void PoseSnapshotProject(void*,const int*,bool first) noexcept {if(first && orderAt<7)order[orderAt++]='S';}
void PoseSnapshotAfter() noexcept {if(orderAt<7)order[orderAt++]='E';}
int recordProjections=0;   // the projections the stand-in recording makes
void* StandInRecord(void* sys,std::uint64_t a,std::uint64_t b) {
    if(orderAt<7)order[orderAt++]='G';
    for(int i=0;i<recordProjections;++i)projectHook(nullptr,nullptr);
    ++recordCalls;recordSys=sys;recordArgs[0]=a;recordArgs[1]=b;
    return reinterpret_cast<void*>(0x77);
}
void Reset() {
    recordState=0;
    for(auto& b:recordBefores)b=nullptr;
    for(auto& a:recordAfters)a=nullptr;
    for(auto& p:recordProjects)p=nullptr;
    recordOpen=false;recordProjections=0;
    matches=redirects=shares=0;sigOk=redirectOk=shareOk=true;redirectedTo=nullptr;projectHook=nullptr;
    image=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(&StandInRecord)-kRecordDraw);
}
void Frame() {
    std::memset(order,0,sizeof(order));orderAt=0;
    using Hook=void*(*)(void*,std::uint64_t,std::uint64_t);
    void* const r=reinterpret_cast<Hook>(redirectedTo)(reinterpret_cast<void*>(0x55),0x1234,1);
    Check(r==reinterpret_cast<void*>(0x77) && recordSys==reinterpret_cast<void*>(0x55) && recordArgs[0]==0x1234 &&
          recordArgs[1]==1,"the game's recording runs with the call's arguments, its result returned");
}

// Both features: one redirect, the frame rate first, the first person second, then the game.
void Both() {
    Reset();
    Check(renderrecord::Install(),"install: the frame rate's");
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Check(renderrecord::Install(),"install: the first person's (already in)");
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    Check(matches==1 && redirects==1 && redirectedTo==reinterpret_cast<void*>(&RecordHook),
          "two features: the call site checked and redirected once, to the single hook");
    Frame();
    Check(!std::strcmp(order,"RFG"),"order: the frame rate's poses, the first person's boxes, then the recording");
    // Registered the other way round: the order is the slots', not the registrations'.
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Frame();
    Check(!std::strcmp(order,"RFG"),"order: by slot whatever installs first");
    Check(shares==1 && projectHook==&ProjectHook,"the projection shared once, to the single hook");
    // The projection turns: in slot order at each projection, `first` at the recording's first only.
    renderrecord::SetProject(renderrecord::Slot::kFirstPerson,&FirstPersonProject);
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&FrameRateProject);
    recordProjections=2;
    Frame();
    Check(!std::strcmp(order,"RFGPQpq"),"projections: both turns in slot order, the recording's first once");
    recordProjections=0;
    Frame();
    std::memset(order,0,sizeof(order));orderAt=0;
    projectHook(nullptr,nullptr);
    Check(!std::strcmp(order,"pq"),"a recording that never projected is closed when it returns: a later projection is no first");
    std::memset(order,0,sizeof(order));orderAt=0;
    recordOpen=false;
    projectHook(nullptr,nullptr);
    Check(!std::strcmp(order,"pq"),"a projection outside the recording: no first");
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,nullptr);
    recordProjections=1;
    Frame();
    Check(!std::strcmp(order,"RFGQ"),"a projection turn cleared: the other's only");
    renderrecord::SetProject(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotProject);
    renderrecord::SetAfter(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotAfter);
    Frame();
    Check(!std::strcmp(order,"RFGQSE"),"pose snapshots seal after other projection hooks and retire after recording");
    recordProjections=0;
    Frame();
    Check(!std::strcmp(order,"RFGE"),"an unprojected recording still closes its pose capture");
}

// Each alone (the frame rate off, or the first person off).
void Alone() {
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    Frame();
    Check(!std::strcmp(order,"FG"),"the first person alone (FrameRateCap=0): its turn, then the recording");
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Frame();
    Check(!std::strcmp(order,"RG"),"the frame rate alone (first person off): its turn, then the recording");
    renderrecord::Set(renderrecord::Slot::kFrameRate,nullptr);
    Frame();
    Check(!std::strcmp(order,"G"),"a turn cleared: only the recording");
}

// Refused: the signature or the redirect; no second try, nothing registers.
void Refused() {
    Reset();
    sigOk=false;
    Check(!renderrecord::Install() && redirects==0,"install: a call site not as read is not redirected");
    sigOk=true;
    Check(!renderrecord::Install() && matches==1,"install: refused once, not retried by the next feature");
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&FrameRateProject);
    Check(!recordBefores[0] && !recordProjects[0],"refused: nothing registers");
    Reset();
    redirectOk=false;
    Check(!renderrecord::Install() && redirects==1,"install: a redirect refused");
    Check(!renderrecord::Install() && redirects==1,"install: the refused redirect not retried");
    Reset();
    shareOk=false;
    Check(!renderrecord::Install() && redirects==0,"install: the projection not shared, the recording not redirected");
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kCount,&FrameRateTurn);
    Check(!recordBefores[0] && !recordBefores[1],"a slot out of range is ignored");
}
}  // namespace

int main() {
    Both();
    Alone();
    Refused();
    std::printf("render_record: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}

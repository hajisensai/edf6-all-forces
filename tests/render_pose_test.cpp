// The exact queue markers and hooked pose readers with private native layout fixtures.
// No game is loaded. The simulated writer overwrites its native arrays before FIFO replay.
#include "../src/render_pose.cpp"
#include <cstdio>
#include <deque>
#include <thread>

namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept {static const Config config{};return config;}
}

namespace edf {
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept {return false;}
void* AllocateNearCode(const void*,const unsigned char*,std::size_t) noexcept {return nullptr;}
bool PatchCode(unsigned char*,const unsigned char*,const unsigned char*,std::size_t,Claim,const char*) noexcept {return false;}
}

namespace crew::renderrecord {
bool Install() noexcept {return false;}
void Set(Slot,Before) noexcept {}
void SetAfter(Slot,Before) noexcept {}
void SetProject(Slot,Project) noexcept {}
}

namespace {
using namespace crew;
using namespace crew::renderpose;
int checks=0,failures=0;
void Check(bool ok,const char* label) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",label);}
}
struct Instance {
    alignas(16) unsigned char bytes[0x40]{};
    Matrix pose[2]{};
    Instance() {
        edf::Put<const void*>(bytes,kBones,bytes);
        edf::Put<std::uint64_t>(bytes,kCount,2);
        edf::Put<Matrix*>(bytes,kPose,pose);
    }
    const PoseList* Source() const noexcept {return reinterpret_cast<const PoseList*>(bytes+0x28);}
    void Set(float mark){pose[1].elements[12]=mark;}
};
std::deque<QueueCallback> queue;
QueueCallback cell=nullptr;
float paletteValue=0.0f,meshValue=0.0f;
bool arguments=false;
float writerMark=0.0f;

void* AllocUnit(void*,int units) {
    Check(units==1,"each marker occupies one native unit");
    return &cell;
}
void CommitUnit(void*,int units) {
    Check(units==1 && cell!=nullptr,"each allocation publishes its marker");
    queue.push_back(cell);
    cell=nullptr;
}
void StockWrite(const unsigned char* inst) {edf::At<Matrix*>(inst,kPose)[1].elements[12]=writerMark;}
std::uintptr_t StockPalette(const unsigned char*,void*,const PoseList* source) {
    paletteValue=source->matrix[1].elements[12];
    return 78;
}
std::uintptr_t StockMesh(void*,void*,int pass,const unsigned char*,const PoseList* source,unsigned char flags) {
    meshValue=source->matrix[1].elements[12];
    arguments=pass==3 && flags==1;
    return 79;
}
void Take() {
    const auto command=queue.front();
    queue.pop_front();
    command(nullptr,nullptr);
}
void Draw(Instance& inst,float expected) {
    const auto palette=Palette(inst.bytes,nullptr,inst.Source());
    const auto mesh=Mesh(nullptr,nullptr,3,inst.bytes,inst.Source(),1);
    Check(palette==78 && mesh==79 && arguments,"both native consumers retain the original ABI");
    Check(paletteValue==expected && meshValue==expected,"both consumers use the same matrix data");
}

void Pipeline() {
    stockWrite=&StockWrite;stockPalette=&StockPalette;stockMesh=&StockMesh;
    allocate=&AllocUnit;commit=&CommitUnit;
    // Only the mock ring functions consume this address.
    unsigned char ring[16]{};
    image=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(ring)-kCommands-kRing);
    Instance body;
    Before();
    writerMark=10;
    Write(body.bytes);
    // The first projection includes final interpolation edits made after the writer.
    body.Set(11);
    Project(nullptr,nullptr,true);
    Project(nullptr,nullptr,false);
    After();
    Check(queue.size()==2 && !frozenPose,"one activation and retirement queued for the recording");

    // Complete a second recording before the first is consumed. Both use the same native instance.
    Before();
    writerMark=20;
    Write(body.bytes);
    Project(nullptr,nullptr,true);
    After();
    body.Set(30);
    Check(queue.size()==4,"two in-flight recordings keep distinct slots");
    Take();
    Draw(body,11);
    Matrix viewmodel[2]{};
    viewmodel[1].elements[12]=31;
    const Override override[]{ {body.bytes,viewmodel[0].elements} };
    const DrawOverrides scope{override,1,nullptr};
    drawOverrides=&scope;
    Draw(body,31);
    const Override nativeOverride[]={{body.bytes,nullptr}};
    const DrawOverrides nested{nativeOverride,1,&scope};
    drawOverrides=&nested;
    Draw(body,30);
    drawOverrides=nested.parent;
    Draw(body,31);
    Matrix custom[2]{};
    custom[1].elements[12]=88;
    const PoseList explicitCustom{0,custom};
    Palette(body.bytes,nullptr,&explicitCustom);
    Mesh(nullptr,nullptr,3,body.bytes,&explicitCustom,1);
    Check(paletteValue==88 && meshValue==88,"an explicit native pose list is not replaced by any snapshot");
    drawOverrides=nullptr;
    Draw(body,11);
    Take();
    Draw(body,30);
    Take();
    Draw(body,20);
    writerMark=21;
    Write(body.bytes);
    Draw(body,21);
    Take();
    Check(queue.empty() && !frozenPose,"the last consumer releases the recording");

    Before();
    Write(body.bytes);
    After();   // loading and canceled recordings can return without any projection
    Check(queue.empty() && !capture,"no-projection recording returns its slot without emitting commands");
}

void BoundedBacklog() {
    Instance body;
    for(int frame=0;frame<3;++frame) {
        Before();
        writerMark=static_cast<float>(frame+1);
        Write(body.bytes);
        Project(nullptr,nullptr,true);
        After();
    }
    Check(queue.size()==6,"all three slots can be held by queued frames");
    Before();
    Write(body.bytes);
    Project(nullptr,nullptr,true);
    After();
    Check(queue.size()==6,"a full in-flight queue does not overwrite or wait for a snapshot slot");
    body.Set(999);
    for(int frame=0;frame<3;++frame) {
        Take();
        Draw(body,static_cast<float>(frame+1));
        Take();
    }
    Check(queue.empty(),"all slots are released in FIFO order");
}

void Lifetime() {
    Frame frame;
    Instance inst;
    std::atomic<bool> opened{false},retire{false};
    inst.Set(40);
    Check(frame.Begin(),"producer claims a free slot");
    const auto key=Identify(inst.bytes);
    frame.Observe(key);
    Check(frame.Seal(&Copy),"producer seals the matrices");
    bool openedOk=false,kept=false;
    std::thread reader([&]{
        openedOk=frame.Open();
        opened.store(true,std::memory_order_release);
        while(!retire.load(std::memory_order_acquire))std::this_thread::yield();
        const auto* pose=frame.Lookup(key);
        kept=pose && pose[1].elements[12]==40;
        frame.Retire();
    });
    while(!opened.load(std::memory_order_acquire))std::this_thread::yield();
    inst.Set(41);
    Check(!frame.Begin(),"a blocked renderer prevents producer reuse of its slot");
    retire.store(true,std::memory_order_release);
    reader.join();
    Check(openedOk && kept && frame.Begin(),"the delayed consumer retained its data, then returned the slot");
    frame.Observe(key);
    edf::Put<std::uint64_t>(inst.bytes,kCount,1);
    Check(!frame.Seal(&Copy) && frame.Begin(),"reused native identity fails closed and releases empty slot");
    frame.Cancel();
}
}

int main() {
    Pipeline();
    BoundedBacklog();
    Lifetime();
    std::printf("render_pose: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}

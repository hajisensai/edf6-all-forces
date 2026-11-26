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
    alignas(16) unsigned char bytes[0x60]{};
    Matrix pose[2]{};
    Instance() {
        edf::Put<const void*>(bytes,kBones,bytes);
        edf::Put<std::uint64_t>(bytes,kCount,2);
        edf::Put<Matrix*>(bytes,kPose,pose);
        for(auto& bone:pose)for(int i=0;i<4;++i)bone.elements[i*5]=1.0f;
    }
    const PoseList* Source() const noexcept {return reinterpret_cast<const PoseList*>(bytes+0x28);}
    void Set(float mark){pose[1].elements[12]=mark;}
};
struct TestBuffer final : ID3D11Buffer {
    ULONG references=1;
    UINT size=2*sizeof(PackedBone);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override {return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {return --references;}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device**) override {}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* type) override {*type=D3D11_RESOURCE_DIMENSION_BUFFER;}
    void STDMETHODCALLTYPE SetEvictionPriority(UINT) override {}
    UINT STDMETHODCALLTYPE GetEvictionPriority() override {return 0;}
    void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* desc) override {
        *desc={size,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};
    }
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
    const float delta[]={1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1};
    Check(stagePose && stagePose(body.bytes,delta) && body.pose[1].elements[12]==11,
          "staging accepts the transform without touching the live skeleton");
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
    Draw(body,15);
    Draw(body,15);
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
    Draw(body,15);
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
    Check(queue.empty() && !capture && !stagePose,"no-projection recording returns its slot without emitting commands");
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

void PaletteOwnership() {
    Frame frame;
    Instance model;
    TestBuffer buffer;
    ID3D11Buffer* holder=&buffer;
    alignas(16) unsigned char records[2*kBoneStride]{};
    edf::Put<void*>(model.bytes,kBones,records);
    edf::Put<void*>(model.bytes,kPaletteHolder,&holder);
    Matrix bind{};
    for(int i=0;i<4;++i)bind.elements[i*5]=1.0f;
    bind.elements[12]=2.0f;
    for(int i=0;i<2;++i)std::memcpy(records+i*kBoneStride+kInverseBind,&bind,sizeof(bind));

    const auto key=Identify(model.bytes);
    model.Set(8);
    Check(frame.Begin(),"a free slot starts a binding capture");
    frame.Observe(key);
    Check(frame.Seal(&Copy,&CapturePalette) && buffer.references==2,
          "sealing copies inverse binds and retains the actual constant buffer");
    std::memset(records,0xA5,sizeof(records));
    model.Set(500);
    buffer.Release();  // the native owner's reference can go before frame retirement
    Check(frame.Open() && buffer.references==1,"the pending frame owns the last buffer reference");
    const auto* frozen=frame.Lookup(key);
    const auto* palette=frame.Palette(key,frozen,&buffer);
    Check(palette && palette[1].columns[3]==10.0f,"packing uses the recorded world matrix and inverse bind");
    Check(frame.Palette(key,frozen,&buffer)==palette,"repeated passes reuse the packed palette");
    TestBuffer replacement;
    Check(!frame.Palette(key,frozen,&replacement) && !frame.Palette(key,model.pose,&buffer),
          "rebinding the buffer or explicitly overriding a pose bypasses the packed palette");
    frame.Retire();
    Check(buffer.references==0,"retirement releases the retained D3D resource");

    holder=&replacement;
    replacement.size+=16;
    Check(frame.Begin(),"the retired slot can be reused");
    frame.Observe(key);
    Check(frame.Seal(&Copy,&CapturePalette) && frame.Open() && replacement.references==1 &&
          !frame.Palette(key,frame.Lookup(key),&replacement),"nonmatching buffer widths preserve the native upload");
    frame.Retire();
}

void ArenaGrowth() {
    struct Model {
        Instance inst;
        alignas(16) unsigned char records[2*kBoneStride]{};
    };
    constexpr std::size_t staged=350,count=600;
    std::vector<Model> models(count);
    TestBuffer buffer;
    ID3D11Buffer* holder=&buffer;
    Frame frame;
    Check(frame.Begin(),"start an empty arena");
    const float delta[]={1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1};
    bool stagedOk=true;
    for(std::size_t i=0;i<count;++i) {
        auto& model=models[i];
        model.inst.Set(static_cast<float>(i));
        edf::Put<void*>(model.inst.bytes,kBones,model.records);
        edf::Put<void*>(model.inst.bytes,kPaletteHolder,&holder);
        Matrix bind{};
        for(int row=0;row<4;++row)bind.elements[row*5]=1;
        bind.elements[12]=static_cast<float>(i+1);
        for(int bone=0;bone<2;++bone)std::memcpy(model.records+bone*kBoneStride+kInverseBind,&bind,sizeof(bind));
        const auto key=Identify(model.inst.bytes);
        frame.Observe(key);
        if(i<staged)stagedOk=frame.Stage(key,delta,&Copy) && stagedOk;
    }
    Check(stagedOk && frame.Seal(&Copy,&CapturePalette) && frame.Open(),"stage and capture across multiple arena growths");
    Check(buffer.references==count+1,"the frame holds a reference for every captured binding");
    bool posesOk=true,palettesOk=true;
    for(std::size_t i=0;i<count;++i) {
        auto& model=models[i];
        model.inst.Set(-1);
        std::memset(model.records,0,sizeof(model.records));
        const auto key=Identify(model.inst.bytes);
        const auto* pose=frame.Lookup(key);
        const float expected=static_cast<float>(i)+(i<staged ? 4.0f : 0.0f);
        posesOk=posesOk && pose && pose[1].elements[12]==expected;
        const auto* packed=frame.Palette(key,pose,&buffer);
        palettesOk=palettesOk && packed && packed[1].columns[3]==expected+static_cast<float>(i+1);
    }
    Check(posesOk && palettesOk,"growth retains captured prefixes and packs each staged transform exactly once");
    frame.Retire();
    Check(buffer.references==1 && frame.Begin(),"retiring all entries frees the frame without moving native ownership");
    const auto last=Identify(models.back().inst.bytes);
    frame.Observe(last);
    Check(frame.Seal(&Copy) && frame.Open() && frame.Lookup(last) && frame.Lookup(last)[1].elements[12]==-1 &&
          !frame.Lookup(Identify(models.front().inst.bytes)),"a smaller later recording never sees the previous arena contents");
    frame.Retire();
}

void RestagedIdentity() {
    Frame frame;
    Instance before,after;
    before.Set(8);after.Set(90);
    const float delta[]={1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1};
    Check(frame.Begin(),"claim a slot for a reused instance");
    const auto old=Identify(before.bytes);
    frame.Observe(old);
    Check(frame.Stage(old,delta,&Copy),"stage the original arrays");
    edf::Put<Matrix*>(before.bytes,kPose,after.pose);
    const auto replacement=Identify(before.bytes);
    frame.Observe(replacement);
    Check(frame.Seal(&Copy) && frame.Open() && !frame.Lookup(old),"a changed identity can only publish its new arrays");
    const auto* pose=frame.Lookup(replacement);
    Check(pose && pose[1].elements[12]==90,"the old staged transform never reaches reused storage");
    frame.Retire();
}

void WarpPaletteUpload() {
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
                                 &device,nullptr,&context))) {
        std::puts("WARP unavailable, skipping software D3D upload check");
        return;
    }
    Instance model;
    alignas(16) unsigned char records[2*kBoneStride]{};
    edf::Put<void*>(model.bytes,kBones,records);
    Matrix bind{};
    for(int i=0;i<4;++i)bind.elements[i*5]=1.0f;
    for(int i=0;i<2;++i)std::memcpy(records+i*kBoneStride+kInverseBind,&bind,sizeof(bind));
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth=2*sizeof(PackedBone);
    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* target=nullptr;
    ID3D11Buffer* readback=nullptr;
    const HRESULT create=device->CreateBuffer(&desc,nullptr,&target);
    desc.BindFlags=0;
    desc.Usage=D3D11_USAGE_STAGING;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    const HRESULT staging=device->CreateBuffer(&desc,nullptr,&readback);
    Check(SUCCEEDED(create) && SUCCEEDED(staging),"software D3D creates matching constant and staging buffers");
    if(target && readback) {
        edf::Put<void*>(model.bytes,kPaletteHolder,&target);
        void* nativeContext[2]={nullptr,context};
        model.Set(42);
        Before();
        // Record the real native identity, then let the producer move the live pose.
        capture->Observe(Identify(model.bytes));
        Project(nullptr,nullptr,true);
        After();
        model.Set(77);
        Take();
        Check(Palette(model.bytes,nativeContext,model.Source())==0,"the covered draw uses the captured upload path");
        context->CopyResource(readback,target);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(SUCCEEDED(context->Map(readback,0,D3D11_MAP_READ,0,&mapped))) {
            const auto* p=static_cast<const PackedBone*>(mapped.pData);
            Check(p[1].columns[3]==42.0f,"D3D received the recorded palette rather than the live pose");
            context->Unmap(readback,0);
        } else Check(false,"the software render buffer can be read back");
        Take();
    }
    if(readback)readback->Release();
    if(target)target->Release();
    context->Release();
    device->Release();
}

void MatrixPacking() {
    Matrix bind{},world{};
    for(int i=0;i<16;++i) {
        bind.elements[i]=static_cast<float>(i-9)*0.25f;
        world.elements[i]=static_cast<float>((i*7)%19-5)*0.125f;
    }
    PackedBone packed{};
    PackBone(bind,world,packed);
    bool equal=true;
    for(int c=0;c<3;++c)for(int r=0;r<4;++r) {
        const auto* b=bind.elements+4*r;
        const auto* p=world.elements+c;
        const float expected=(b[0]*p[0]+b[1]*p[4])+(b[2]*p[8]+b[3]*p[12]);
        equal=equal && packed.columns[c*4+r]==expected;
    }
    Check(equal,"all 12 outputs match the native inverse-bind product and column layout");
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
    PaletteOwnership();
    ArenaGrowth();
    RestagedIdentity();
    MatrixPacking();
    WarpPaletteUpload();
    Lifetime();
    std::printf("render_pose: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}

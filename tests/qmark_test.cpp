// The custom Q mark (src/qmark.cpp) on stand-ins: no game, no transport. The production qmark.cpp runs against recorders
// for what it calls (the camera, the flight readouts, the native ID reader and resolver, the lock points, the sender, the
// sounds), so these checks hold the code the plugin runs:
//  - the aim's ray: through the mouse's aim point in a heli's / a jet's / a rotor craft's mouse-aim flight, else the
//    screen's centre (qmark_ray.h; the user, 2026-10-10: "这个q应该在鼠标位置");
//  - the wire (qmark_protocol.h): round trip, another magic not ours, a newer version and unknown capability bits ignored,
//    the sender's keepalive / change / gap, the receiver's order, restart and silence, a point's time;
//  - sharing: this machine's marks sent to every peer, a teammate's shown (found here by its native ID, its slot only for
//    the authenticated sender), an old peer's bytes (no QMRK) left to the support parser;
//  - the cues: marking asks for the own cue, a teammate's new mark for the team cue, at QMarkVolume; 0: silent.
#include "../src/qmark.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
namespace {
Config config{};
unsigned char humanMem[0x400]{},enemyMem[0x400]{},enemyCtrl[0x20]{};
float camEye[3]={0,10,0},camDir[3]={0,0,1};
bool haveCamera=true;
PlayerJetReadout jetHud{};bool jetHudOn=false;
PlayerHeliReadout heli{};bool heliOn=false;
// This machine's mark (npcai.cpp's).
bool ownEnemy=false;float ownAt[3]={5,0,300};
// Native IDs: the player's, the enemy's (the same on every machine).
const unsigned char kPlayerId[32]={0x11,0x22,0,0,0,0,0,0,0,0,0,0,5,0,0,0,7,0,0,0,0,0,0,0,9,9};
const unsigned char kEnemyId[32]={0x33,0x44,0,0,0,0,0,0,0,0,0,0,5,0,0,0,8,0,0,0,0,0,0,0,1,2};
const unsigned char kTeamId[32]={0x55,0x66,0,0,0,0,0,0,0,0,0,0,5,0,0,0,9,0,0,0,0,0,0,0,3,4};
// The resolver: whether the teammate's player and the enemy are in this world, the player's slot and PUID.
bool resolveTeam=true,resolveEnemy=true;int resolves=0;
int teamPuid=0;
const char* senderPuid="0002aaaabbbbcccc";
float enemyLock[3]={40,5,500};
ULONGLONG frame=1;
// The recorders.
int sends=0;std::uint32_t sentTo[8]{};unsigned char sent[qmark_net::kWireSize]{};
int played[audio::kClipCount]{};float playedGain=0.0f;
}  // namespace
const Config& Cfg() noexcept { return config; }
unsigned char* PlayerHuman() noexcept { return humanMem; }
ULONGLONG GameFrame() noexcept { return frame; }
void Log(const char*,...) noexcept {}
bool CameraRayOf(const void* who,float* eye,float* dir) noexcept {
    if(!haveCamera || who!=humanMem)return false;
    std::memcpy(eye,camEye,12);std::memcpy(dir,camDir,12);return true;
}
bool CameraRay(float*,float*) noexcept { return false; }
bool PlayerJetHud(PlayerJetReadout* out) noexcept { if(jetHudOn)*out=jetHud;return jetHudOn; }
bool PlayerHeliHud(PlayerHeliReadout* out) noexcept { if(heliOn)*out=heli;return heliOn; }
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out) noexcept {
    if(object==humanMem){std::memcpy(out,kPlayerId,32);return true;}
    if(object==enemyMem){std::memcpy(out,kEnemyId,32);return true;}
    return false;
}
bool ResolveMarkIdentities(const unsigned char marker[32],const unsigned char target[32],MarkIdentities* out) noexcept {
    ++resolves;*out=MarkIdentities{};
    if(resolveTeam && marker && !std::memcmp(marker,kTeamId,32)){out->player=ObjRef{humanMem,enemyCtrl};out->slot=2;out->puid=&teamPuid;}
    if(resolveEnemy && target && !std::memcmp(target,kEnemyId,32))out->target=ObjRef::Of(enemyMem);
    return true;
}
bool SupportCommandRequesterMatches(void* puid,const char* sender) noexcept { return puid==&teamPuid && !std::strcmp(sender,senderPuid); }
// The name tag's name (player_name.cpp): the teammate's player's, a Japanese one; asked for which object.
const void* namedWho=nullptr;bool nameReadable=true;
bool ReadPlayerName(const void* soldier,wchar_t* out,std::size_t count) noexcept {
    namedWho=soldier;
    if(!nameReadable){out[0]=0;return false;}
    const wchar_t* n=L"さくら_EDF";
    FitName(n,std::wcslen(n),out,count);return true;
}
bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept { visit(ctx,enemyMem,enemyLock);return true; }
bool NpcMarkOwn(const void** object,float* at) noexcept {
    if(!ownEnemy)return false;
    *object=enemyMem;std::memcpy(at,ownAt,12);return true;
}
float GameEffectVolume() noexcept { return 1.0f; }
namespace audio {
bool ClipsReady() noexcept { return true; }
void Beat(float) noexcept {}
void PlayOnce(int clip,const Heard& h) noexcept { if(clip>=0 && clip<kClipCount)++played[clip];playedGain=h.left; }
}  // namespace audio
}  // namespace crew

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }
bool Send(std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    if(sends<8)sentTo[sends]=peer;
    ++sends;
    if(size==sizeof(sent))std::memcpy(sent,bytes,size);
    return true;
}
float Angle(const float* a,const float* b) {
    const float d=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    return std::acos(std::fmax(-1.0f,std::fmin(1.0f,d/std::sqrt((a[0]*a[0]+a[1]*a[1]+a[2]*a[2])*(b[0]*b[0]+b[1]*b[1]+b[2]*b[2])))));
}
void Frame() { ++frame;QMarkFrame(); }
int Views(QMarkView* v) { return QMarkViews(v,kQMarkViews); }
// A teammate's state as its machine would send it.
std::size_t TeamBytes(unsigned char* out,std::uint32_t sequence,bool enemy,bool point,std::uint32_t origin=77,std::uint32_t letGo=0) {
    qmark_net::State s;s.origin=origin;s.sequence=sequence;std::memcpy(s.marker,kTeamId,32);s.letGo=letGo;
    if(enemy){s.enemy=true;std::memcpy(s.target,kEnemyId,32);s.enemyAt[0]=1;s.enemyAt[1]=2;s.enemyAt[2]=3;}
    if(point){s.point=true;s.pointAt[0]=-50;s.pointAt[1]=0;s.pointAt[2]=80;s.pointLeftMs=5000;}
    return qmark_net::Encode(s,out,qmark_net::kWireSize) ? qmark_net::kWireSize : 0;
}

void RayChecks() {
    float eye[3],dir[3];
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,camDir)<1e-4f && eye[1]==10.0f,"on foot: the screen's centre from the drawn camera's eye");
    heliOn=true;heli.f.aiming=true;heli.f.aim[0]=200;heli.f.aim[1]=10;heli.f.aim[2]=800;
    const float toHeli[3]={200,0,800};
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,toHeli)<1e-4f,"a stock heli's mouse-aim flight: through its FLY square, not the centre");
    heli.f.aiming=false;
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,camDir)<1e-4f,"a stock heli not mouse-flown: the centre");
    jetHudOn=true;jetHud.rotor=false;jetHud.aiming=true;jetHud.aim[0]=-300;jetHud.aim[1]=110;jetHud.aim[2]=700;
    const float toJet[3]={-300,100,700};
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,toJet)<1e-4f,"a fighter's mouse aim: through its aim square");
    jetHud.rotor=true;jetHud.aiming=false;jetHud.heli.aiming=true;jetHud.heli.aim[0]=0;jetHud.heli.aim[1]=410;jetHud.heli.aim[2]=400;
    const float toRotor[3]={0,400,400};
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,toRotor)<1e-4f,"a rotor craft's mouse-aim flight: through its FLY square");
    jetHud.heli.aiming=false;
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,camDir)<1e-4f,"a rotor craft flown by the keys: the centre");
    jetHud.rotor=false;jetHud.aiming=true;jetHud.aim[0]=0;jetHud.aim[1]=10;jetHud.aim[2]=-500;   // behind the camera (a look back)
    Check(QMarkRay(humanMem,eye,dir) && Angle(dir,camDir)<1e-4f,"an aim point behind the camera: the centre");
    jetHudOn=heliOn=false;
    Check(!QMarkRay(&enemyMem[0],eye,dir),"no camera drawn for that soldier: no ray");
    haveCamera=false;Check(!QMarkRay(humanMem,eye,dir),"no camera yet: no ray");haveCamera=true;
    const float c[3]={0,0,2},aim[3]={camEye[0],camEye[1],camEye[2]+0.5f};
    Check(qmark::AimRay(camEye,c,nullptr,dir) && dir[2]==1.0f,"AimRay: no aim, the centre normalized");
    Check(qmark::AimRay(camEye,c,aim,dir) && dir[2]==1.0f,"AimRay: an aim point under a metre off the eye, the centre");
}

void WireChecks() {
    using namespace qmark_net;
    State s;s.origin=9;s.sequence=4;std::memcpy(s.marker,kPlayerId,32);s.enemy=true;std::memcpy(s.target,kEnemyId,32);
    s.enemyAt[0]=1.5f;s.enemyAt[1]=-2;s.enemyAt[2]=3e3f;s.point=true;s.pointAt[0]=7;s.pointAt[2]=-8;s.pointLeftMs=4321;
    unsigned char b[kWireSize];State d;
    Check(Encode(s,b,sizeof(b)) && Owned(b,sizeof(b)) && Decode(b,sizeof(b),d) && d.origin==9 && d.sequence==4 && d.enemy && d.point &&
          !std::memcmp(d.marker,kPlayerId,32) && !std::memcmp(d.target,kEnemyId,32) && d.enemyAt[2]==3e3f && d.pointAt[2]==-8.0f &&
          d.pointLeftMs==4321 && d.caps==kCapabilities,"round trip");
    Check(b[0]=='Q' && b[1]=='M' && b[2]=='R' && b[3]=='K',"magic QMRK on the wire");
    unsigned char c[kWireSize];std::memcpy(c,b,sizeof(c));c[4]=2;
    Check(Owned(c,sizeof(c)) && !Decode(c,sizeof(c),d),"a newer version: ours, not taken");
    std::memcpy(c,b,sizeof(c));c[8]|=0x80;
    Check(Decode(c,sizeof(c),d) && d.caps==kCapabilities && d.enemy,"an unknown capability bit: masked off, the rest taken");
    std::memcpy(c,b,sizeof(c));c[8]=static_cast<unsigned char>(kCapPoint);
    Check(Decode(c,sizeof(c),d) && !d.enemy && d.point,"a peer without kCapEnemy: its enemy flag not taken");
    std::memcpy(c,b,sizeof(c));c[0]='S';
    Check(!Owned(c,sizeof(c)) && !Decode(c,sizeof(c),d),"another magic (the support / command messages): not ours");
    Check(!Decode(b,sizeof(b)-1,d),"the wrong size: not taken");
    std::memcpy(c,b,sizeof(c));c[56+21]=1;Check(!Decode(c,sizeof(c),d),"a target ID not canonical: not taken");
    std::memcpy(c,b,sizeof(c));c[88]=0;c[89]=0;c[90]=0xC0;c[91]=0x7F;Check(!Decode(c,sizeof(c),d),"a NaN position: not taken");
    State bad=s;bad.pointLeftMs=kMaxPointMs+1;Check(!Encode(bad,b,sizeof(b)),"a point past its longest time: not sent");
    // The sender.
    Outbox o(5);State m;std::memcpy(m.marker,kPlayerId,32);
    Check(o.Due(m,1000),"the first state goes at once");
    State t=o.Stamp(m,1000);
    Check(t.origin==5 && t.sequence==1,"stamped with the run and the sequence");
    Check(!o.Due(m,1500) && o.Due(m,2000),"nothing marked: a keepalive a second");
    o.Stamp(m,2000);m.enemy=true;std::memcpy(m.target,kEnemyId,32);
    Check(!o.Due(m,2000+kMinGapMs-1) && o.Due(m,2000+kMinGapMs),"a change: at once, but never within the gap");
    o.Stamp(m,2100);m.enemyAt[0]=50;
    Check(!o.Due(m,2200) && o.Due(m,2100+kEnemyKeepMs),"an enemy marked: its position again every kEnemyKeepMs");
    // The receiver.
    Inbox in;Change ch;
    State r;r.origin=3;r.sequence=10;r.enemy=true;std::memcpy(r.target,kEnemyId,32);
    Check(in.Receive(1,r,100,&ch) && ch==Change::marked && in.Enemy(1,100),"a new enemy: marked");
    r.sequence=11;r.enemyAt[0]=99;
    Check(in.Receive(1,r,200,&ch) && ch==Change::none,"the same enemy moved: no new mark");
    r.sequence=9;Check(!in.Receive(1,r,300,&ch),"a late message of the same run: dropped");
    r.sequence=12;r.enemy=false;Check(in.Receive(1,r,400,&ch) && ch==Change::cleared && !in.Enemy(1,400),"let go: cleared");
    r.origin=4;r.sequence=1;r.point=true;r.pointLeftMs=1000;
    Check(in.Receive(1,r,500,&ch) && ch==Change::marked && in.Point(1,1499) && !in.Point(1,1500),"a restarted sender taken; its point only for its time");
    Check(!in.Enemy(1,500+kSilentMs) && !in.Capable(1,500+kSilentMs,kCapEnemy) && in.Capable(1,600,kCapEnemy),"a silent peer: nothing kept");
    Check(!in.Receive(0,r,600) && !in.Receive(kMaxPeers+1,r,600),"no peer 0 (this machine) or past the peers");
    {   // let-go: its count moved within one run; a restarted run's count is no let-go.
        Inbox li;State a;a.origin=1;a.sequence=1;a.enemy=true;std::memcpy(a.target,kEnemyId,32);
        li.Receive(1,a,100,&ch);
        a.sequence=2;a.enemy=false;a.letGo=1;Check(li.Receive(1,a,200,&ch) && ch==Change::letGo,"let go: letGo");
        a.sequence=3;a.enemy=true;li.Receive(1,a,300,&ch);
        a.sequence=4;a.enemy=false;Check(li.Receive(1,a,400,&ch) && ch==Change::cleared,"gone, the count the same: cleared");
        a.origin=2;a.sequence=1;a.enemy=true;li.Receive(1,a,500,&ch);
        a.origin=3;a.sequence=1;a.enemy=false;a.letGo=7;Check(li.Receive(1,a,600,&ch) && ch==Change::cleared,"another run's count: no let-go");
        State w=a;w.caps=kCapEnemy|kCapPoint;unsigned char lb[kWireSize];
        Check(Encode(w,lb,sizeof(lb)) && Decode(lb,sizeof(lb),d) && d.letGo==0,"a peer without kCapLetGo: its count not read");
    }
    {   // The name fitted for the HUD.
        wchar_t fit[6];
        FitName(L"abc",3,fit,6);Check(!std::wcscmp(fit,L"abc"),"FitName: a short name as it is");
        FitName(L"abcdefgh",8,fit,6);Check(!std::wcscmp(fit,L"abcd…"),"FitName: a long one cut with an ellipsis");
        FitName(L"a\nb\tc",5,fit,6);Check(!std::wcscmp(fit,L"abc"),"FitName: control characters dropped");
        FitName(L"x",1,fit,1);Check(fit[0]==0,"FitName: no room: empty");
    }
}

void ShareChecks() {
    // The production clock (GetTickCount64): the times handed in are its, as support_net.cpp hands them.
    const ULONGLONG T=GetTickCount64()-1000;   // every time below at or before the clock's now, within kSilentMs of it
    config.enabled=true;config.npcMarkKey=0x51;config.qmarkVolume=0.8f;config.qmarkPointSec=15.0f;
    Put<void*>(enemyMem,kSelfCtrl,enemyCtrl);Put<const void*>(enemyMem,kSelf,enemyMem);Put<long>(enemyCtrl,8,1);Put<long>(enemyCtrl,0xC,1);
    ResetQMarks();
    QMarkView v[kQMarkViews];
    // Offline: our marks shown, no network.
    ownEnemy=true;Frame();
    int n=Views(v);
    Check(n==1 && v[0].own && v[0].enemy && v[0].at[2]==300.0f,"offline: this machine's enemy mark shown");
    float point[3]={10,0,20};QMarkSetPoint(point);Frame();n=Views(v);
    Check(n==2 && v[1].own && !v[1].enemy && v[1].at[0]==10.0f,"offline: this machine's point mark shown");
    // The cues.
    std::memset(played,0,sizeof(played));
    QMarkPlay(QMarkCue::own);Frame();
    Check(played[audio::kClipMarkOwn]==1 && std::fabs(playedGain-0.8f)<1e-6f,"marking: the own cue at QMarkVolume");
    QMarkPlay(QMarkCue::off);Frame();Check(played[audio::kClipMarkOff]==1,"letting go: the off cue");
    Frame();Check(played[audio::kClipMarkOwn]==1 && played[audio::kClipMarkOff]==1,"a cue plays once");
    config.qmarkVolume=0.0f;QMarkPlay(QMarkCue::own);Frame();Check(played[audio::kClipMarkOwn]==1,"QMarkVolume=0: silent");
    config.qmarkVolume=0.8f;
    // Online: sent to every peer.
    sends=0;UpdateQMarkNetwork(true,2,&Send,T);
    qmark_net::State d;
    Check(sends==2 && sentTo[0]==1 && sentTo[1]==2,"online: this machine's marks to every peer");
    Check(qmark_net::Decode(sent,sizeof(sent),d) && !std::memcmp(d.marker,kPlayerId,32) && d.enemy && !std::memcmp(d.target,kEnemyId,32) &&
          d.point && d.pointAt[2]==20.0f && d.pointLeftMs>0,"the state sent: the marker, the enemy by its native ID, the point and its time");
    sends=0;UpdateQMarkNetwork(true,2,&Send,T+10);Check(sends==0,"not again within the gap");
    UpdateQMarkNetwork(true,2,&Send,T+qmark_net::kEnemyKeepMs);Check(sends==2,"the enemy's position again on its keepalive");
    // A teammate's.
    unsigned char b[qmark_net::kWireSize];
    std::memset(played,0,sizeof(played));resolves=0;
    Check(TeamBytes(b,1,true,false) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+300),"a teammate's state: taken");
    Frame();n=Views(v);
    bool team=false;
    for(int i=0;i<n;++i)if(!v[i].own && v[i].enemy)team=v[i].slot==2 && v[i].at[2]==500.0f;
    Check(team,"the teammate's enemy found here by its ID, at its lock point here, with its player's slot");
    bool named=false;
    for(int i=0;i<n;++i)if(!v[i].own && v[i].enemy)named=!std::wcscmp(v[i].name,L"さくら_EDF");
    Check(named && namedWho==humanMem,"...named as the game's name tag names its player (the resolved marker's own object)");
    {   // The NPCs' side: the teammate's marked enemy offered to them as this machine's mark is (npcai.cpp NearestMark).
        const void* e[4];
        Check(QMarkTeamEnemies(e,4)==1 && e[0]==enemyMem,"the teammate's marked enemy offered to the NPCs");
    }
    Check(played[audio::kClipMarkTeam]==1,"a teammate marked: the team cue");
    Check(TeamBytes(b,2,true,false) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+400),"the same mark again");
    Frame();Check(played[audio::kClipMarkTeam]==1 && resolves==1,"the same mark: no cue, not looked for again");
    Check(TeamBytes(b,3,true,true) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+500),"its point too");
    Frame();n=Views(v);
    bool teamPoint=false;
    for(int i=0;i<n;++i)if(!v[i].own && !v[i].enemy)teamPoint=v[i].slot==2 && v[i].at[0]==-50.0f;
    Check(teamPoint && played[audio::kClipMarkTeam]==2,"the teammate's point shown, its cue");
    // Another peer claiming the same player: shown, but not as that player.
    Check(TeamBytes(b,1,true,false,88) && ReceiveQMarkNetwork(2,"0003ffff",b,sizeof(b),T+600),"another peer's state");
    Frame();n=Views(v);
    int unknown=0;for(int i=0;i<n;++i)unknown+=!v[i].own && v[i].slot<0;
    Check(unknown==1,"a marker that is not its sender's player: no slot (ALLY)");
    bool unnamed=false;
    for(int i=0;i<n;++i)if(!v[i].own && v[i].slot<0)unnamed=v[i].name[0]==0;
    Check(unnamed,"...and no name: a peer cannot name another player");
    {
        const void* e[4];
        Check(QMarkTeamEnemies(e,4)==1,"two teammates on one enemy: offered once");
    }
    // The enemy dead here: the teammate's mark not shown.
    enemyMem[kDead]=1;Frame();n=Views(v);
    int teamEnemies=0;for(int i=0;i<n;++i)teamEnemies+=!v[i].own && v[i].enemy;
    Check(teamEnemies==0,"the teammates' enemy dead here: their marks of it not shown");
    {
        const void* e[4];
        Check(QMarkTeamEnemies(e,4)==0,"...nor offered to the NPCs");
    }
    enemyMem[kDead]=0;
    // Not found here (not registered yet): shown where its marker sent it.
    ResetQMarkNetwork();UpdateQMarkNetwork(true,2,&Send,T+700);resolveEnemy=false;
    Check(TeamBytes(b,1,true,false) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+710),"a teammate's enemy not in this world");
    Frame();n=Views(v);team=false;
    for(int i=0;i<n;++i)if(!v[i].own && v[i].enemy)team=v[i].at[2]==3.0f;
    Check(team,"shown at the position its marker sent");
    resolveEnemy=true;
    {
        const void* e[4];
        Check(QMarkTeamEnemies(e,4)==0,"an enemy not found here: shown, not offered to the NPCs (nothing here to fight)");
    }
    // Let go (the user, 2026-10-10: the teammate's let-go heard as ours; a mark gone with its enemy silent).
    std::memset(played,0,sizeof(played));
    Check(TeamBytes(b,2,false,false,77,1) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+720),"the teammate let its mark go");
    Frame();Check(played[audio::kClipMarkOff]==1 && played[audio::kClipMarkTeam]==0,"a teammate's let-go: the off cue");
    Check(TeamBytes(b,3,true,false,77,1) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+730),"it marks again");
    Frame();std::memset(played,0,sizeof(played));
    Check(TeamBytes(b,4,false,false,77,1) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+740),"its mark gone with its enemy (no let-go)");
    Frame();Check(played[audio::kClipMarkOff]==0 && played[audio::kClipMarkTeam]==0,"a mark gone with its enemy: silent");
    Check(TeamBytes(b,6,false,false,77,3) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+750),"a let-go whose message was lost, then another");
    Frame();Check(played[audio::kClipMarkOff]==1,"the let-goes counted, not each message: one cue");
    // This machine's let-go counted for the teammates.
    Check(qmark_net::Decode(sent,sizeof(sent),d),"the last state sent");
    const std::uint32_t before=d.letGo;
    sends=0;QMarkPlay(QMarkCue::off);Frame();UpdateQMarkNetwork(true,2,&Send,T+760);
    Check(sends==2 && qmark_net::Decode(sent,sizeof(sent),d) && d.letGo==before+1,"this machine's let-go counted and sent at once");
    // The name not readable: P<n> (no name).
    nameReadable=false;ResetQMarkNetwork();UpdateQMarkNetwork(true,2,&Send,T+770);
    Check(TeamBytes(b,1,true,false) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+780),"heard again");
    Frame();n=Views(v);named=true;
    for(int i=0;i<n;++i)if(!v[i].own && v[i].enemy)named=v[i].name[0]!=0 || v[i].slot!=2;
    Check(!named,"a name that cannot be read: none (the HUD says P<n>)");
    nameReadable=true;
    // What is not ours.
    unsigned char other[qmark_net::kWireSize]{};other[0]='S';
    Check(!ReceiveQMarkNetwork(1,senderPuid,other,sizeof(other),T+800),"another magic: left to the support / command parsers");
    TeamBytes(b,9,true,false);b[4]=2;
    std::memset(played,0,sizeof(played));
    Check(ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+810),"a newer version: ours, dropped");
    Frame();Check(played[audio::kClipMarkTeam]==0,"...no cue for it");
    Check(TeamBytes(b,10,true,false) && ReceiveQMarkNetwork(9,senderPuid,b,sizeof(b),T+820),"a peer past the room: dropped");
    // The teammate silent: gone.
    Frame();ResetQMarkNetwork();UpdateQMarkNetwork(true,2,&Send,T+900);
    Check(TeamBytes(b,1,true,false) && ReceiveQMarkNetwork(1,senderPuid,b,sizeof(b),T+900),"heard");
    Frame();n=Views(v);int seen=0;for(int i=0;i<n;++i)seen+=!v[i].own;
    Check(seen==1,"its mark shown");
    // Room gone, the plugin off.
    UpdateQMarkNetwork(false,0,&Send,T+950);Frame();n=Views(v);seen=0;for(int i=0;i<n;++i)seen+=!v[i].own;
    Check(seen==0,"the room gone: the teammates' marks gone");
    config.npcMarkKey=0;sends=0;UpdateQMarkNetwork(true,2,&Send,T+1000);Frame();
    Check(sends==0 && Views(v)==0,"the key off (NpcMarkKey=0): nothing marked, shown or sent");
    config.npcMarkKey=0x51;
    ResetQMarks();ownEnemy=false;Frame();Check(Views(v)==0,"a new mission: no marks");
    Put<void*>(enemyMem,kSelfCtrl,nullptr);
}
}  // namespace

int main() {
    RayChecks();
    WireChecks();
    ShareChecks();
    std::printf("qmark_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}

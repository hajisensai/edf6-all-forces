// The debug spawn tool's decisions (src/debug_spawn.h) without the game: the catalog is whole (every category has rows,
// every native row its stock SGO, the plugin's rows an argument in their enum's range), the menu goes round as it says
// (shut it reads only its toggle, one press is one step, picks kept per category, wrap both ways), a spawn lands on the
// crosshair's point or ahead of the player, faces the way it should, and its matrix is a proper rotation. The tool is
// off unless the ini says so (crew.h Config). Exit code 1 when one fails.
// cmake --build build --target debug_spawn_test && build\debug_spawn_test.exe
#include "../src/debug_spawn.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
using namespace crew::debugspawn;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool Near(float a,float b,float e=1e-4f) { return std::fabs(a-b)<=e; }

void Catalog() {
    int total=0;
    for(int c=0;c<kCategoryCount;++c) {
        const int n=CountIn(static_cast<Category>(c));
        Expect(n>0,"every category has rows");
        total+=n;
        for(int i=0;i<n;++i) {
            const int row=RowOf(static_cast<Category>(c),i);
            Expect(row>=0 && kEntries[row].category==static_cast<Category>(c),"RowOf finds the category's rows in order");
        }
        Expect(RowOf(static_cast<Category>(c),n)==-1,"RowOf past the category: none");
    }
    Expect(total==kEntryCount,"every row in one category");
    for(int i=0;i<kEntryCount;++i) {
        const Entry& e=kEntries[i];
        const bool native=e.how==How::stockVehicle || e.how==How::enemy;
        Expect(native==(e.sgo!=nullptr),"a native row names its SGO, a plugin row none");
        if(e.sgo)Expect(std::wcsncmp(e.sgo,L"app:/object/",12)==0,"the SGO is an app:/object/ path");
        Expect(e.name && e.name[0] && e.id && e.id[0],"a name and a log id");
        Expect(std::isfinite(e.lift) && e.lift>=0.0f && e.lift<=200.0f,"a sane lift");
        // The category says which kind of spawn it is (the faction and placement follow it).
        if(e.how==How::stockVehicle)Expect(e.category==Category::vehicle,"stock vehicles are vehicles");
        if(e.how==How::enemy)Expect(e.category==Category::enemy,"enemies are enemies");
        if(e.how==How::jetRole || e.how==How::drone || e.how==How::heli)Expect(e.category==Category::aircraft,"the plugin's aircraft");
        if(e.how==How::soldier)Expect(e.category==Category::soldier,"soldiers");
        if(e.how==How::jetRole)Expect(e.arg>=0 && e.arg<8,"a JetRole (8 roles, crew.h)");
        if(e.how==How::heli)Expect(e.arg>=0 && e.arg<3,"a HeliBody (3 bodies, crew.h)");
        if(e.how==How::soldier)Expect(e.arg>=0 && e.arg<5,"a SupportWeapon (5 weapons, support_call.h)");
        for(int k=0;k<i;++k)Expect(std::strcmp(kEntries[k].id,e.id)!=0,"log ids are unique");
    }
}

Keys Only(bool toggle,bool prev,bool next,bool category,bool spawn) { return Keys{toggle,prev,next,category,spawn}; }

void MenuSteps() {
    Menu m{};
    Expect(!m.open,"the menu starts shut");
    Expect(Step(m,Only(false,true,true,true,true))==Act::none && !m.open && m.pick[0]==0 && m.category==0,
           "shut: only the toggle does anything (F5/F6/F7/F9 are the game's then)");
    Expect(Step(m,Only(true,false,false,false,true))==Act::opened && m.open,"the toggle opens it (and wins over spawn)");
    const int vehicles=CountIn(Category::vehicle);
    Expect(Step(m,Only(false,true,false,false,false))==Act::moved && m.pick[0]==vehicles-1,"prev from the first row wraps to the last");
    Expect(Step(m,Only(false,false,true,false,false))==Act::moved && m.pick[0]==0,"next from the last wraps to the first");
    Step(m,Only(false,false,true,false,false));Step(m,Only(false,false,true,false,false));
    Expect(m.pick[0]==2 && Picked(m)==RowOf(Category::vehicle,2),"two nexts: the third vehicle");
    Expect(Step(m,Only(false,false,false,true,false))==Act::moved && m.category==1,"the category key: the next category");
    Expect(m.pick[1]==0 && Picked(m)==RowOf(Category::aircraft,0),"a category starts at its first row");
    Step(m,Only(false,false,true,false,false));
    for(int i=0;i<kCategoryCount;++i)Step(m,Only(false,false,false,true,false));   // all the way round
    Expect(m.category==1 && m.pick[1]==1 && m.pick[0]==2,"round the categories: each kept its pick");
    Step(m,Only(false,false,false,true,false));Step(m,Only(false,false,false,true,false));Step(m,Only(false,false,false,true,false));
    Expect(m.category==0 && Picked(m)==RowOf(Category::vehicle,2),"back on vehicles, the third still picked");
    Expect(Step(m,Only(false,true,true,true,true))==Act::spawn && m.pick[0]==2 && m.category==0,
           "spawn asks for the picked row and moves nothing, whatever else is pressed");
    Expect(Step(m,Only(true,false,false,false,false))==Act::closed && !m.open,"the toggle shuts it");
    Expect(Step(m,Only(false,false,false,false,true))==Act::none,"shut: spawn does nothing");
    m.open=true;m.category=99;
    Expect(Step(m,Only(false,false,false,false,false))==Act::none && m.category==0,"a broken category resets to the first");
    m.pick[0]=999;
    Expect(Step(m,Only(false,false,true,false,false))==Act::moved && m.pick[0]==1,"a broken pick resets, then moves");

    // A key held over frames is one press (Press: the key's own state).
    bool held=false;
    const bool down[]={false,true,true,true,false,true,false};
    int presses=0;
    for(bool d:down)presses+=Press(held,d);
    Expect(presses==2,"a held key is one press; released and pressed again, a second");
}

void Placement() {
    const float player[3]={100.0f,10.0f,200.0f};
    const float dir[3]={0.0f,-0.6f,0.8f};
    Spot s{};
    const float aimed[3]={100.0f,0.0f,260.0f};
    Expect(PlaceSpawn(player,dir,aimed,true,kMinSpawnDistance,30.0f,&s) && !s.snap && Near(s.at[2],260.0f) && Near(s.at[1],0.0f),
           "a crosshair point 60 m out: there, as hit (no snap)");
    const float feet[3]={101.0f,9.0f,203.0f};
    Expect(PlaceSpawn(player,dir,feet,true,kMinSpawnDistance,30.0f,&s) && s.snap && Near(s.at[0],100.0f) && Near(s.at[2],230.0f) &&
           Near(s.at[1],10.0f),"a crosshair point at the player's feet: 30 m ahead, snapped to the ground");
    Expect(PlaceSpawn(player,dir,nullptr,false,kMinSpawnDistance,30.0f,&s) && s.snap && Near(s.at[2],230.0f),
           "no hit (sky / past the range): ahead");
    const float up[3]={0.0f,1.0f,0.0f};
    Expect(!PlaceSpawn(player,up,nullptr,false,kMinSpawnDistance,30.0f,&s),"straight up with nothing hit: no place");

    const float at[3]={100.0f,0.0f,260.0f},north[3]={0.0f,0.0f,1.0f};
    float h[3];
    HeadingTo(player,at,north,h);
    Expect(Near(h[0],0.0f) && Near(h[2],1.0f) && h[1]==0.0f,"a vehicle faces away from the player");
    HeadingTo(at,player,north,h);
    Expect(Near(h[2],-1.0f),"an enemy faces the player");
    HeadingTo(player,player,dir,h);
    Expect(Near(h[2],1.0f) && Near(h[0],0.0f),"on one spot: the fallback, made horizontal and unit");
    const float diag[3]={0.6f,0.0f,0.8f};
    float m[16];
    FacingMatrix(diag,at,m);
    // Orthonormal rows, determinant 1 (support_soldier.cpp Matrix refuses anything else), forward = the heading.
    float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    Expect(Near(det,1.0f),"the matrix is a rotation (determinant 1)");
    Expect(Near(m[8],0.6f) && Near(m[10],0.8f) && Near(m[0]*m[8]+m[2]*m[10],0.0f),"forward is the heading, right is across it");
    Expect(Near(m[12],100.0f) && Near(m[13],0.0f) && Near(m[14],260.0f) && m[15]==1.0f && m[3]==0.0f && m[7]==0.0f && m[11]==0.0f,
           "the position row, the affine column");
}
}  // namespace

int main() {
    Catalog();
    MenuSteps();
    Placement();
    std::printf("debug_spawn_test: %d checks, %d failed (%d rows: %d vehicles, %d aircraft, %d enemies, %d soldiers)\n",checks,failures,
                kEntryCount,CountIn(Category::vehicle),CountIn(Category::aircraft),CountIn(Category::enemy),CountIn(Category::soldier));
    return failures ? 1 : 0;
}

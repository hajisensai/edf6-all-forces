// The stock HUD's switch while the map shows (src/map_stock_hud.h, docs/hud-re.md §11) checked offline: a camera's
// +0x200 byte driven through the frames the camera hook sees (map.cpp StockHud: hide while the map's view is on it) and
// the scripts' SetPlayerHudShow writes (HudShowHook): held at 0 while the map shows, put back when it stops, whatever
// stopped it (the map closed, the hook's fault dropping camSide, a new mission), and a mission's own hide or show made
// while the map was open honoured when it closes. A second local player's camera is never written.
//   cmake --build build --target map_hud_check && build\map_hud_check.exe      (exit code 1 on a failure)
#include "../src/map_stock_hud.h"
#include <cstdio>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,int a=0,int b=0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%d, %d)\n",what,a,b);
}

// A camera: its switch byte (the constructor's 1, 0x118B03A).
struct Cam { unsigned char shown=1; };

// The game's write as HudShowHook makes it: through the record, else straight into the byte.
void GameSet(maphud::Record& r,Cam& c,std::uint64_t gen,bool show) {
    if(maphud::GameSet(r,&c,gen,show))c.shown=show ? 1 : 0;
}
// `n` camera steps with the map showing on it or not.
bool Steps(maphud::Record& r,Cam& c,std::uint64_t gen,bool hide,int n=1) {
    bool held=false;
    for(int i=0;i<n;++i)held=maphud::Step(r,&c,gen,hide,&c.shown);
    return held;
}
}  // namespace

int main() {
    // Open (24 ease-in frames, held), close (15 ease-out frames still the map's view: still held), back.
    {
        maphud::Record r;Cam c;
        Check(!Steps(r,c,1,false,5) && c.shown==1,"closed: the switch untouched",c.shown);
        Check(Steps(r,c,1,true,24+60) && c.shown==0,"open: the stock HUD off",c.shown);
        Check(Steps(r,c,1,true,15) && c.shown==0,"easing back: still off",c.shown);
        Check(!Steps(r,c,1,false) && c.shown==1 && !r.hidden,"the camera back: the stock HUD on",c.shown);
        Check(!Steps(r,c,1,false,10) && c.shown==1,"and it stays on",c.shown);
    }
    // A fault in the camera hook drops camSide: the next step has hide false and puts the switch back.
    {
        maphud::Record r;Cam c;
        Steps(r,c,1,true,10);
        Check(!Steps(r,c,1,false) && c.shown==1,"a fault mid-map: the stock HUD back on the next step",c.shown);
    }
    // A cutscene's hide while the map is open: the byte stays 0, the hide is kept and is what the close leaves.
    {
        maphud::Record r;Cam c;
        Steps(r,c,1,true,10);
        GameSet(r,c,1,false);
        Check(c.shown==0 && r.want==false,"a hide during the map: kept",c.shown,r.want);
        Steps(r,c,1,true,10);
        Steps(r,c,1,false);
        Check(c.shown==0,"the map closed in the cutscene: the HUD stays hidden as the mission asked",c.shown);
        GameSet(r,c,1,true);
        Check(c.shown==1,"the cutscene's end shows it (no hold left)",c.shown);
    }
    // Opened in a cutscene (the mission's switch already 0), the mission shows the HUD while the map is open: shown at
    // the close, not before.
    {
        maphud::Record r;Cam c;
        GameSet(r,c,1,false);
        Steps(r,c,1,true,10);
        Check(r.want==false && c.shown==0,"opened over a hidden HUD: remembered hidden",c.shown,r.want);
        GameSet(r,c,1,true);
        Check(c.shown==0,"the mission's show during the map waits",c.shown);
        Steps(r,c,1,false);
        Check(c.shown==1,"...and lands at the close",c.shown);
    }
    // Something else writes 1 while held (not through SetPlayerHudShow): taken as the game's latest, held at 0 again.
    {
        maphud::Record r;Cam c;
        GameSet(r,c,1,false);
        Steps(r,c,1,true,3);
        c.shown=1;
        Check(Steps(r,c,1,true) && c.shown==0 && r.want,"an unseen write: held again, remembered as shown",c.shown,r.want);
        Steps(r,c,1,false);
        Check(c.shown==1,"...and shown at the close",c.shown);
    }
    // A second local player's camera: never written, the hold stays the first one's.
    {
        maphud::Record r;Cam a,b;
        b.shown=1;
        Steps(r,a,1,true,5);
        Check(Steps(r,b,1,false,5) && b.shown==1 && a.shown==0,"another camera: untouched",b.shown,a.shown);
        GameSet(r,b,1,false);
        Check(b.shown==0 && r.want,"the mission's write to another camera goes straight through",b.shown,r.want);
        Steps(r,a,1,false);
        Check(a.shown==1,"the first camera back on",a.shown);
    }
    // A new mission while held (ResetMap: the generation moves). The same camera stepped again: shown (its hold was a
    // show). A fresh camera at the same address with the mission's own hide made first: the hide stands.
    {
        maphud::Record r;Cam c;
        Steps(r,c,1,true,10);
        Check(!Steps(r,c,2,false) && c.shown==1 && !r.hidden,"a new mission: the old hold let go, shown",c.shown);
    }
    {
        maphud::Record r;Cam c;
        Steps(r,c,1,true,10);
        c.shown=1;                 // the old camera gone, a new one built at its address (its constructor: 1)
        GameSet(r,c,2,false);      // the new mission's opening cutscene hides the HUD before the camera's first step
        Check(c.shown==0 && !r.hidden,"a stale hold does not swallow the new mission's write",c.shown);
        Steps(r,c,2,false);
        Check(c.shown==0,"...nor undo it at the first step",c.shown);
    }
    {
        maphud::Record r;Cam c;
        GameSet(r,c,1,false);
        Steps(r,c,1,true,10);      // held over a hidden HUD...
        c.shown=1;                 // ...the camera rebuilt for the next mission at the same address
        Steps(r,c,2,false);
        Check(c.shown==1,"a stale hidden hold is not written onto a new camera",c.shown);
    }
    // Opened again after a close: a fresh hold, and the map opened in the next mission holds too.
    {
        maphud::Record r;Cam c;
        Steps(r,c,1,true,5);Steps(r,c,1,false);
        Check(Steps(r,c,1,true) && c.shown==0,"opened again: off",c.shown);
        Check(Steps(r,c,2,true) && c.shown==0 && r.generation==2,"a new mission with the map open: held anew",c.shown);
        Steps(r,c,2,false);
        Check(c.shown==1,"closed: on",c.shown);
    }
    std::printf(failures ? "map_hud_check: %d of %d FAILED\n" : "map_hud_check: %d checks passed\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}

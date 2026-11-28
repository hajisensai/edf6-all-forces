// The Rune Blade's ether gauge (docs/newclass-re/runeblade-plan.md §1.4; included by hud.cpp inside namespace hudi): a bar
// under the crosshair while this machine's player is a Rune Blade, its level / max, cyan; amber while depleted; the glide
// and the steps taken since leaving the ground beside it. Nothing while not a Rune Blade (RuneBladeHudNow: none fresh).
void RuneBladeHud(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    RuneBladeHudView v;
    if(!RuneBladeHudNow(&v) || v.max<=0.0f)return;
    const float share=v.level<=0.0f ? 0.0f : v.level>=v.max ? 1.0f : v.level/v.max;
    const float w=220.0f*s,h=8.0f*s,x=width*0.5f-w*0.5f,y=height*0.5f+250.0f*s;
    const float* const fill=v.depleted ? kAmber : kCyan;
    Bar(drawer,ctx,x,y,w,h,share,share,fill,s);
    Label(text,lines,at,width*0.5f,y-18.0f*s,1,kLineScale*0.7f,fill,Tr(Tx::rbEtherTitle),
          static_cast<double>(v.level),static_cast<double>(v.max));
    if(v.depleted)Label(text,lines,at,width*0.5f,y+14.0f*s,1,kLineScale*0.65f,kAmber,L"%ls",Tr(Tx::rbEtherDepleted));
    else if(v.gliding)Label(text,lines,at,width*0.5f,y+14.0f*s,1,kLineScale*0.65f,kTeal,Tr(Tx::rbEtherGlide),v.steps);
}

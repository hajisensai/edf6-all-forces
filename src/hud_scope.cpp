// The vehicle HUD's on-foot sights (split from hud.cpp; the HUD's overview is hud.cpp's head): a magnified sight's
// eyepiece and shade (scopeview.h), the soldier's open scope (InfantryScope) and the rescue cue's banner.
#include "hud_internal.h"

namespace crew {
namespace hudi {
// The sea rescue (heli.cpp): asked for, on its way, or why no heli came; a moment, under the sweep's banner.
void RescueCueBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    RescueCue c{};
    if(!PlayerRescueCue(&c))return;
    Label(text,lines,at,width*0.5f,height*0.5f+254.0f*s,1,kLineScale,c.bad ? kAmber : kGreen,L"%ls",c.text);
}
}  // namespace hudi


// A magnified sight's picture (scopeview.h; sightzoom.cpp). Redone 2026-10-11 as real optics look: a round field for an
// optic (a gunner's sight, a scope): the eyepiece's black all round (opaque, as an eyepiece is), the image darkening
// over the field's last few percent into it (scopeview.h kFeather*: no drawn rim), the reticle etched on the glass
// (scopereticle.h, drawn with the weapon's marks: ScopeArm / InfantryScope). An electronic sensor (an energy weapon's
// display, the gunship gunner's, a weapon station's monitor) is the whole view (scopeview.h RectHalf; 2026-10-25 no
// longer a 4:3 box in black bands): a thin bezel just inside the view's edge, corner marks, its field-of-view name.
// A monocular telescope's (2026-10-30: "有些是单眼看的瞄具") a smaller field in its eyecup's black ring, the view round
// the cup only dimmed (scopeview.h kMono*): the other eye sees what is round the sight.
alignas(16) const float kScopeDark[4]={0.0f,0.0f,0.0f,0.97f};
alignas(16) const float kScopeSurround[4]={0.0f,0.0f,0.0f,scopeview::kSurroundAlpha};
alignas(16) const float kScopeFeather[scopeview::kFeathers][4]={
    {0.0f,0.0f,0.0f,scopeview::kFeatherAlpha[0]},{0.0f,0.0f,0.0f,scopeview::kFeatherAlpha[1]},
    {0.0f,0.0f,0.0f,scopeview::kFeatherAlpha[2]},{0.0f,0.0f,0.0f,scopeview::kFeatherAlpha[3]}};
alignas(16) const float kLensFeather[scopepip::kFeathers][4]={
    {0.0f,0.0f,0.0f,scopepip::kFeatherAlpha[0]},{0.0f,0.0f,0.0f,scopepip::kFeatherAlpha[1]},
    {0.0f,0.0f,0.0f,scopepip::kFeatherAlpha[2]},{0.0f,0.0f,0.0f,scopepip::kFeatherAlpha[3]}};
alignas(16) const float kScopeBezel[4]={0.75f,0.8f,0.78f,0.55f};
void Quad4(void* drawer,void* ctx,const scopeview::Quad& q,const float* rgba) noexcept {
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={q.x[0],q.y[0],0.0f, q.x[1],q.y[1],0.0f, q.x[2],q.y[2],0.0f, q.x[3],q.y[3],0.0f};
    Emit(drawer,ctx,m,rgba,v);
}
// The round field's eyepiece round (cx, cy), radius r (see above): a monocular one's black only out to its cup.
void Eyepiece(void* drawer,void* ctx,float width,float height,float cx,float cy,float r,bool monocular) noexcept {
    const float out=scopeview::Reach(width,height,cx,cy),cup=monocular ? scopeview::CupOut(r,true) : out;
    for(int b=0;b<scopeview::kFeathers;++b)
        for(int i=0;i<scopeview::kSides;++i)
            Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,r*scopeview::FeatherFrom(b),r*scopeview::FeatherTo(b)),kScopeFeather[b]);
    for(int i=0;i<scopeview::kSides;++i)Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,r,cup),kScopeDark);
    if(monocular)for(int i=0;i<scopeview::kSides;++i)Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,cup,out),kScopeSurround);
}
void ScopeShade(void* drawer,void* ctx,Text* text,float width,float height,float s,float zoom,sightzoom::Mask mask,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f;
    if(mask==sightzoom::Mask::sensor) {
        float hx,hy;
        scopeview::RectHalf(width,height,&hx,&hy);
        // A thin bezel, a corner mark at each corner, the field of view named (narrow / medium / wide) with its power.
        const float t=1.2f*s,c=28.0f*s;
        Seg(drawer,ctx,cx-hx,cy-hy,cx+hx,cy-hy,t,kScopeBezel);Seg(drawer,ctx,cx-hx,cy+hy,cx+hx,cy+hy,t,kScopeBezel);
        Seg(drawer,ctx,cx-hx,cy-hy,cx-hx,cy+hy,t,kScopeBezel);Seg(drawer,ctx,cx+hx,cy-hy,cx+hx,cy+hy,t,kScopeBezel);
        for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
            const float x=cx+sx*(hx-10.0f*s),y=cy+sy*(hy-10.0f*s);
            Seg(drawer,ctx,x,y,x-sx*c,y,2.0f*s,kScopeBezel);Seg(drawer,ctx,x,y,x,y-sy*c,2.0f*s,kScopeBezel);
        }
        Label(text,lines,at,cx-hx+(18.0f+28.0f+12.0f)*s,cy-hy+22.0f*s,0,kLineScale*0.85f,kScopeBezel,
              Tr(zoom>=5.0f ? Tx::scopeFovNarrow : zoom>=2.0f ? Tx::scopeFovMedium : Tx::scopeFovWide),zoom);
        return;
    }
    const bool monocular=mask==sightzoom::Mask::monocular;
    Eyepiece(drawer,ctx,width,height,cx,cy,scopeview::Radius(width,height,monocular),monocular);
    (void)s;(void)zoom;(void)text;(void)lines;(void)at;
}

// A soldier's open scope (infantryscope.h): for the HUD pass of the soldier looking through it, the eyepiece and the
// weapon's optic etched across the field round the screen's centre (the camera looks along the weapon's line).
void InfantryScope(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    InfantryScopeCue c{};
    if(!InfantryScopeNow(&c))return;
    __try {
        if(passCamera && (!Readable(passCamera,0x368) ||
           (At<const void*>(passCamera,0x350)!=c.human && At<const void*>(passCamera,0x360)!=c.human)))return;   // another player's view
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    const float cx=width*0.5f,cy=height*0.5f;
    float r,focal;
    if(c.lens) {
        // The picture in the picture (scope_pip.h): the world round the lens unmagnified and clear; the lens's last tenth
        // darkening into the scope's tube (its rim, kRim of the radius, black); the reticle's angles the scope's FOV's
        // (the lens shows the stock zoomed camera's picture), not the frame's round it.
        r=scopepip::LensRadius(width,height);
        for(int b=0;b<scopepip::kFeathers;++b)
            for(int i=0;i<scopeview::kSides;++i)
                Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,r*scopepip::FeatherFrom(b),r*scopepip::FeatherTo(b)),kLensFeather[b]);
        for(int i=0;i<scopeview::kSides;++i)Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,r,scopepip::RimOut(r)),kScopeDark);
        focal=scopepip::LensFocal(height,c.lensFov);
    } else {
        const bool monocular=infantryscope::EyepieceOf(c.look)==sightzoom::Eyepiece::monocular;
        r=scopeview::Radius(width,height,monocular);
        Eyepiece(drawer,ctx,width,height,cx,cy,r,monocular);
        focal=CentreFocal(vp,width,height);
        if(!(focal>0.0f))focal=0.5f*height/std::tan(0.5f*sightzoom::kBaseFov/c.zoom);   // the stock camera's vertical FOV
    }
    const reticle::View v{cx,cy,focal,s,kLineScale};
    reticle::Sketch k{};
    scope::Draw(c.look,reticle::Aim{},v,scope::Field{cx,cy,r},&k);
    Render(drawer,ctx,text,s,k,lines,at);
}
}  // namespace crew

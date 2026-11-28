// The Vehicler's logistics block (vehicler_logistics.h VehiclerHudNow; docs/newclass-re/vehicler-plan.md §3.3): at the
// right, above the stock weapon gauges. The title and the points against the gauge, the bar (green once the points
// cover the vehicle picked), the vehicle picked and its price, the keys; a resupply on its way: how far; for a moment
// what came of a call. Nothing unless this machine's player is a Vehicler.
namespace hudi {
void VehiclerHud(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    VehiclerHudView v{};
    if(!VehiclerHudNow(&v))return;
    if(!LineSink{lines,at}.Room(6,__func__))return;
    const float right=width-40.0f*s,w=260.0f*s,row=24.0f*s;
    float y=height*0.56f;
    if(!v.heard) {
        Label(text,lines,at,right,y,2,kLineScale*0.85f,kAmber,L"%ls",Tr(Tx::zwLogWaiting));
        return;
    }
    const bool covers=v.price>0 && v.points>=v.price;
    Label(text,lines,at,right,y,2,kLineScale*0.9f,covers ? kGreen : kWhite,Tr(Tx::zwLogTitle),v.points,v.gauge);
    y+=row*0.75f;
    const float share=v.gauge ? static_cast<float>(v.points)/static_cast<float>(v.gauge) : 0.0f;
    Bar(drawer,ctx,right-w,y-5.0f*s,w,10.0f*s,share,share,covers ? kGreen : kAmber,s);
    if(v.price && v.gauge && v.price<=v.gauge) {   // the price's mark on the bar
        const float px=right-w+w*static_cast<float>(v.price)/static_cast<float>(v.gauge);
        Rect(drawer,ctx,px-1.0f*s,y-8.0f*s,px+1.0f*s,y+8.0f*s,kYellow);
    }
    y+=row*0.85f;
    if(v.name[0])Label(text,lines,at,right,y,2,kLineScale*0.8f,covers ? kGreen : kAmber,Tr(Tx::zwLogPick),v.name,v.price);
    y+=row*0.8f;
    wchar_t key[48]{};
    if(v.key)KeyName(v.key,key,_countof(key));
    if(v.button>0) {
        wchar_t pad[24]{};
        PadBitsName(v.button,pad,_countof(pad));
        const std::size_t n=std::wcslen(key);
        std::swprintf(key+n,_countof(key)-n,n ? L" / %ls" : L"%ls",pad);
    }
    if(v.transit) {
        if(v.distance>=0.0f)Label(text,lines,at,right,y,2,kLineScale*0.8f,kCyan,Tr(Tx::zwLogComing),static_cast<double>(v.distance));
        else Label(text,lines,at,right,y,2,kLineScale*0.8f,kCyan,L"%ls",Tr(Tx::zwLogDispatched));
    } else if(key[0] && v.name[0]) {
        Label(text,lines,at,right,y,2,kLineScale*0.7f,kTeal,Tr(Tx::zwLogCall),key,v.name);
        if(v.choices>1){y+=row*0.7f;Label(text,lines,at,right,y,2,kLineScale*0.7f,kTeal,Tr(Tx::zwLogCycle),key);}
    }
    y+=row*0.8f;
    if(v.notice!=VehiclerNotice::none) {
        const Tx words=v.notice==VehiclerNotice::arrived ? Tx::zwLogArrived : v.notice==VehiclerNotice::refused ? Tx::zwLogRefused :
                       Tx::zwLogPoor;
        Label(text,lines,at,right,y,2,kLineScale*0.8f,v.notice==VehiclerNotice::arrived ? kGreen : kAmber,L"%ls",Tr(words));
    }
}
}  // namespace hudi

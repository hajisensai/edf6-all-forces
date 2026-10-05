// Included by playerjet.cpp after kKinds, in its file namespace: the Kind of each aircraft of playerjet_kinds.h
// (kBoardable, the NPC's performance), and which of them a vehicle is.

constexpr Kind KindFrom(const pjet::Perf& p) noexcept {
    return Kind{p.name,p.mark,p.minAir,p.rotate,p.top,p.thrust,p.brake,p.maxG,p.corner,p.roll,p.landMax,p.ram};
}
template<std::size_t... I> struct BoardKinds {
    static constexpr Kind rows[sizeof...(I)]={KindFrom(pjet::kBoardable[I].perf)...};
};
template<std::size_t N,std::size_t... I> struct BoardKindsOf : BoardKindsOf<N-1,N-1,I...> {};
template<std::size_t... I> struct BoardKindsOf<0,I...> { using type=BoardKinds<I...>; };
using BoardKindTable=BoardKindsOf<static_cast<std::size_t>(pjet::kBoardableCount)>::type;
constexpr const Kind* kBoardKinds=BoardKindTable::rows;

// The derivation checked against the hand-tuned player jets: the NPC fighter and strike jet come out within 10%.
constexpr bool Near(float a,float b) noexcept { return a<=b*1.1f && a>=b*0.9f; }
constexpr bool NearKind(const Kind& a,const Kind& b) noexcept {
    return Near(a.minAir,b.minAir) && Near(a.rotate,b.rotate) && Near(a.top,b.top) && Near(a.thrust,b.thrust) && Near(a.brake,b.brake) &&
           Near(a.maxG,b.maxG) && Near(a.corner,b.corner) && Near(a.roll,b.roll) && Near(a.landMax,b.landMax);
}
static_assert(NearKind(KindFrom(pjet::Row(jet::Body::fighter)->perf),kKinds[0]),"the derivation strays from the player fighter");
static_assert(NearKind(KindFrom(pjet::Row(jet::Body::strike)->perf),kKinds[1]),"the derivation strays from the player strike jet");

// The jet body `v` is (its mark; the strike jets that took over a bomber share the strike jet's mark and are told by
// their model's bones, crew.h BomberBody), when it is one the player may board: its row, else nullptr.
const pjet::Boardable* BoardRowOf(const unsigned char* v) noexcept {
    if(BodyOf(v)!=PluginBody::jet || !Cfg().playerJetAll)return nullptr;
    const float mark=BodyMark(v);
    for(int i=0;i<static_cast<int>(sizeof(jet::kBodies)/sizeof(jet::kBodies[0]));++i) {
        const jet::BodyRow& row=jet::kBodies[i];
        if(row.mark<=0.0f || row.mark!=mark)continue;
        jet::Body body=row.body;
        if(body==jet::Body::strike || body==jet::Body::bomber401 || body==jet::Body::bomber501_2) {
            const JetBody model=BomberBody(v+kModelInst506);
            body=model==JetBody::bomber401 ? jet::Body::bomber401 : model==JetBody::bomber501_2 ? jet::Body::bomber501_2 : jet::Body::strike;
        }
        return pjet::Row(body);
    }
    return nullptr;
}
const Kind* BoardKindOf(const unsigned char* v) noexcept {
    const pjet::Boardable* row=BoardRowOf(v);
    return row ? &kBoardKinds[row-pjet::kBoardable] : nullptr;
}

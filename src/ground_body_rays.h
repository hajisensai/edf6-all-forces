#pragma once

namespace crew {

// One fixed map body resolved for a validated box during a synchronous navigation call.
class GroundBodyRays {
public:
    bool Capture(const float* lower,const float* upper) noexcept;
    bool Unobstructed(const float* from,const float* to) const noexcept;
    // The caller has already checked that the whole segment is in the captured box.
    float FloorHit(const float* from,const float* to,float* point,float* normal) const noexcept;

private:
    void Cast(const float* from,const float* to,void* collector) const noexcept;
    struct Context { const void* dispatcher;const void* filter;const void* codec; };
    Context context{};
    const void* body=nullptr;
};

}

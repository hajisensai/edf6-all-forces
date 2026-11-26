#pragma once

namespace crew {

// One fixed map body looked up for a short, synchronous navigation edge.
class GroundBodyRays {
public:
    bool Capture(const float* lower,const float* upper) noexcept;
    bool Unobstructed(const float* from,const float* to) const noexcept;

private:
    struct Context { const void* dispatcher;const void* filter;const void* codec; };
    Context context{};
    const void* body=nullptr;
};

}

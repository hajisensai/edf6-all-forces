#pragma once

namespace crew {

// One fixed map body resolved for a validated box during a synchronous navigation call.
class GroundBodyRays {
public:
    GroundBodyRays()=default;
    GroundBodyRays(const GroundBodyRays&)=delete;
    GroundBodyRays& operator=(const GroundBodyRays&)=delete;

    static bool InstallScratch() noexcept;
    bool Capture(const float* lower,const float* upper) noexcept;
    bool Unobstructed(const float* from,const float* to) const noexcept;
    // The caller has already checked that the whole segment is in the captured box.
    float FloorHit(const float* from,const float* to,float* point,float* normal) const noexcept;

private:
    void Cast(const float* from,const float* to,void* collector) const noexcept;
    struct Context { const void* dispatcher;const void* filter;const void* codec; };
    struct Scratch {
        using Release=void(*)(void*);
        Release release=nullptr;
        void* triangles[2]{};
        unsigned char initial[2][0x110];
        bool rejected=false;

        ~Scratch() { Reset(); }
        void Reset() noexcept {
            if(!release)return;
            release(triangles[0]);release(triangles[1]);
            release=nullptr;
        }
    };
    static thread_local Scratch* pendingScratch;
    static void* InitScratch(void* native) noexcept;

    Context context{};
    mutable Scratch scratch;
    const void* body=nullptr;
};

}

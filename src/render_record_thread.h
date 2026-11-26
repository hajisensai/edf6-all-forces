// Exclusive transfer of the live scene to the native recording thread.
// The producer waits until the recording returns. The GPU replay remains on
// EDF's existing consumer thread and can run alongside subsequent simulation.
#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <exception>
#include <process.h>
#include <xmmintrin.h>

namespace crew::renderrecord {
class RecordingThread {
public:
    using Draw=void*(*)(void*,std::uint64_t,std::uint64_t);

    bool Start() noexcept {
        if(worker_)return true;
        request_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        complete_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(request_ && complete_) {
            worker_=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,&Worker,this,0,&workerId_));
            if(worker_)return true;
        }
        CloseHandles();
        return false;
    }

    unsigned Id() const noexcept {return workerId_;}

    // Only after every entry point has stopped submitting.
    void Stop() noexcept {
        if(!worker_)return;
        AcquireSRWLockExclusive(&submit_);
        if(!SetEvent(request_) || WaitForSingleObject(worker_,INFINITE)!=WAIT_OBJECT_0)std::terminate();
        CloseHandles();
        ReleaseSRWLockExclusive(&submit_);
    }

    void* Invoke(Draw draw,void* system,std::uint64_t a,std::uint64_t b) {
        if(!worker_ || GetCurrentThreadId()==workerId_)return draw(system,a,b);
        Job job;
        job.draw=draw;job.system=system;job.a=a;job.b=b;
        job.mxcsr=_mm_getcsr();
        job.lastError=GetLastError();
        AcquireSRWLockExclusive(&submit_);
        pending_.store(&job,std::memory_order_release);
        const HANDLE handles[]={complete_,worker_};
        if(!SetEvent(request_) || WaitForMultipleObjects(2,handles,FALSE,INFINITE)!=WAIT_OBJECT_0 ||
           !job.finished.load(std::memory_order_acquire))std::terminate();
        ReleaseSRWLockExclusive(&submit_);
        _mm_setcsr(job.mxcsr);
        SetLastError(job.lastError);
        if(job.cppFailure)std::rethrow_exception(job.cppFailure);
        if(job.fault.ExceptionCode)RaiseException(job.fault.ExceptionCode,
            job.fault.ExceptionFlags&EXCEPTION_NONCONTINUABLE,job.fault.NumberParameters,job.fault.ExceptionInformation);
        return job.result;
    }

private:
    struct Job {
        Draw draw=nullptr;
        void* system=nullptr;
        std::uint64_t a=0,b=0;
        unsigned mxcsr=0;
        DWORD lastError=0;
        void* result=nullptr;
        std::exception_ptr cppFailure;
        EXCEPTION_RECORD fault{};
        std::atomic<bool> finished{false};
    };

    static void CallCpp(Job& job) noexcept {
        try {job.result=job.draw(job.system,job.a,job.b);}
        catch(...) {job.cppFailure=std::current_exception();}
    }

    static int SaveFault(Job& job,const EXCEPTION_POINTERS* fault) noexcept {
        job.fault=*fault->ExceptionRecord;
        job.fault.ExceptionRecord=nullptr;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void CallNative(Job& job) noexcept {
        const unsigned previousMxcsr=_mm_getcsr();
        _mm_setcsr(job.mxcsr);
        SetLastError(job.lastError);
        __try {CallCpp(job);}
        __except(SaveFault(job,GetExceptionInformation())) {}
        job.lastError=GetLastError();
        job.mxcsr=_mm_getcsr();
        _mm_setcsr(previousMxcsr);
    }

    static unsigned __stdcall Worker(void* context) noexcept {
        auto& lane=*static_cast<RecordingThread*>(context);
        SetThreadDescription(GetCurrentThread(),L"EDF6 render recording");
        while(WaitForSingleObject(lane.request_,INFINITE)==WAIT_OBJECT_0) {
            auto* const job=lane.pending_.exchange(nullptr,std::memory_order_acquire);
            if(!job)return 0;
            CallNative(*job);
            job->finished.store(true,std::memory_order_release);
            if(!SetEvent(lane.complete_))return 1;
        }
        return 1;
    }

    void CloseHandles() noexcept {
        if(worker_)CloseHandle(worker_);
        if(complete_)CloseHandle(complete_);
        if(request_)CloseHandle(request_);
        worker_=complete_=request_=nullptr;
        workerId_=0;
    }

    SRWLOCK submit_=SRWLOCK_INIT;
    std::atomic<Job*> pending_{nullptr};
    HANDLE request_=nullptr,complete_=nullptr,worker_=nullptr;
    unsigned workerId_=0;
};
}

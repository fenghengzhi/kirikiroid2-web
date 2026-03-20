//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#define NOMINMAX

#include "tjsCommHead.h"

// #include <process.h>
#include <algorithm>

#include "ThreadIntf.h"
#include "ThreadImpl.h"
#include "MsgIntf.h"
#include "DebugIntf.h"

#if defined(CC_TARGET_OS_IPHONE) || defined(__aarch64__)
#else
// #define USING_THREADPOOL11
#endif

#ifdef USING_THREADPOOL11
#include "threadpool11/pool.hpp"
#endif

#include <thread>

//---------------------------------------------------------------------------
// tTVPThread : a wrapper class for thread
//---------------------------------------------------------------------------
tTVPThread::tTVPThread(bool suspended) {
    Terminated = false;
    Suspended = suspended;

    try {
        Handle = std::thread([this] { StartProc(this); });
        Handle.detach();
    } catch(const std::system_error &) {
        TVPThrowInternalError;
    }
}

//---------------------------------------------------------------------------
tTVPThread::~tTVPThread() {
    // CloseHandle(Handle);
}

//---------------------------------------------------------------------------
void *tTVPThread::StartProc(void *arg) {
    auto *_this = (tTVPThread *)arg;
    if(_this->Suspended) {
        std::unique_lock lk(_this->_mutex);
        _this->_cond.wait(lk);
    }
    _this->Execute();
    TVPOnThreadExited();
    return nullptr;
}

//---------------------------------------------------------------------------
void tTVPThread::WaitFor() {
    if(Handle.joinable()) {
        Handle.join();
    }
}

//---------------------------------------------------------------------------
tTVPThreadPriority tTVPThread::GetPriority() {
    // TODO: impl
    return ttpNormal;
}

//---------------------------------------------------------------------------
void tTVPThread::SetPriority(tTVPThreadPriority pri) {
    // TODO: impl
}

//---------------------------------------------------------------------------
// void tTVPThread::Suspend()
// {
// 	SuspendThread(Handle);
// }
//---------------------------------------------------------------------------
void tTVPThread::Resume() {
    Suspended = false;
    _cond.notify_one();
    // while((tjs_int32)ResumeThread(Handle) > 1) ;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
void tTVPThreadEvent::Set() {
    std::unique_lock lk(Mutex);
    Signaled = true;
    Handle.notify_one();
}

//---------------------------------------------------------------------------
void tTVPThreadEvent::WaitFor(tjs_uint timeout) {
    // wait for event;
    // returns true if the event is set, otherwise (when timed out)
    // returns false.

    std::unique_lock lk(Mutex);
    if(Signaled) {
        Signaled = false;
        return;
    }
    if(timeout != 0) {
        Handle.wait_for(lk, std::chrono::milliseconds(timeout),
                        [this] { return Signaled; });
    } else {
        Handle.wait(lk, [this] { return Signaled; });
    }
    Signaled = false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tjs_int TVPDrawThreadNum = 1;

//---------------------------------------------------------------------------
static tjs_int GetProcesserNum() {
    static tjs_int processor_num = 0;
    if(!processor_num) {
        processor_num = std::thread::hardware_concurrency();
        tjs_char tmp[34];
        TVPAddLog(ttstr(TJS_W("Detected CPU core(s): ")) +
                  TJS_tTVInt_to_str(processor_num, tmp));
    }
    return processor_num;
}

tjs_int TVPGetProcessorNum() { return GetProcesserNum(); }

//---------------------------------------------------------------------------
tjs_int TVPGetThreadNum() {
    tjs_int threadNum = TVPDrawThreadNum ? TVPDrawThreadNum : GetProcesserNum();
#ifdef __EMSCRIPTEN__
    // On Web platform, limit thread count to reduce thread pool pressure
    // OpenMP will use this value, so we cap it at 4 threads
    threadNum = std::min(threadNum, 4);
#else
    threadNum = std::min(threadNum, TVPMaxThreadNum);
#endif
    return threadNum;
}

//---------------------------------------------------------------------------
void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func) {
    if(numThreads == 1) {
        func(0);
        return;
    }
#if !defined(USING_THREADPOOL11)
#pragma omp parallel for schedule(static)
    for(int i = 0; i < numThreads; ++i)
        func(i);
#else
    static threadpool11::Pool pool;
    std::vector<std::future<void>> futures;
    for(int i = 0; i < numThreads; ++i) {
        futures.emplace_back(pool.postWork<void>(std::bind(func, i)));
    }
    for(auto &it : futures)
        it.get();
#endif
#if 0
    ThreadInfo *threadInfo;
    threadInfo = TVPThreadList[TVPThreadTaskCount++];
    threadInfo->lpStartAddress = func;
    threadInfo->lpParameter = param;
    InterlockedIncrement(&TVPRunningThreadCount);
    while (ResumeThread(threadInfo->thread) == 0)
      Sleep(0);
#endif
}
//---------------------------------------------------------------------------

std::vector<std::function<void()>> _OnThreadExitedEvents;

void TVPOnThreadExited() {
    for(const auto &ev : _OnThreadExitedEvents) {
        ev();
    }
}

void TVPAddOnThreadExitEvent(const std::function<void()> &ev) {
    _OnThreadExitedEvents.emplace_back(ev);
}

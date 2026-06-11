//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// safe 64bit System Tick Count
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsUtils.h"
#include "TickCount.h"
#include "SysInitIntf.h"
#include "ThreadIntf.h"

#define DWORD uint32_t
//---------------------------------------------------------------------------
// 64bit may enough to hold usual time count.
// ( 32bit is clearly insufficient )
//---------------------------------------------------------------------------
static tjs_uint64 TVPTickCountBias = 0;
static DWORD TVPWatchLastTick;
static tTJSCriticalSection TVPTickWatchCS;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static DWORD TVPCheckTickOverflow() {
    DWORD curtick;
    { // thread-protected
        tTJSCriticalSectionHolder holder(TVPTickWatchCS);

        curtick = TVPGetRoughTickCount32();
#ifdef __EMSCRIPTEN__
        // 平台边界（Web）：主线程返回 vsync 锁相 tick、worker 返回原始时钟
        // （见 cpp/core/environ/web/Platform.cpp），两时钟域相差 ±数 ms，
        // 跨线程交错调用会观察到毫秒级回退；仅当回退幅度达 2^31 才判定为
        // 真 32bit 溢出，避免把时钟域偏差当成 wrap（bias 误加 49.7 天）。
        if(curtick < TVPWatchLastTick &&
           (TVPWatchLastTick - curtick) > 0x80000000UL) {
#else
        if(curtick < TVPWatchLastTick) {
#endif
            // timeGetTime() was overflowed
            TVPTickCountBias += 0x100000000L; // add 1<<32
        }
        TVPWatchLastTick = curtick;
    } // end-of-thread-protected
    return curtick;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class tTVPWatchThread : public tTVPThread {
    // thread which watches overflow of 32bit counter of
    // TVPGetRoughTickCount32

    tTVPThreadEvent Event;

public:
    tTVPWatchThread();

    ~tTVPWatchThread() override;

protected:
    void Execute() override;

} static *TVPWatchThread = nullptr;

//---------------------------------------------------------------------------
tTVPWatchThread::tTVPWatchThread() : tTVPThread(true) {
    TVPWatchLastTick = TVPGetRoughTickCount32();
    SetPriority(ttpNormal);
    Resume();
}

//---------------------------------------------------------------------------
tTVPWatchThread::~tTVPWatchThread() {
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
}

//---------------------------------------------------------------------------
void tTVPWatchThread::Execute() {
    while(!GetTerminated()) {
        TVPCheckTickOverflow();

        Event.WaitFor(0x10000000);
        // 0x10000000 will be enough to watch timeGetTime()'s counter
        // overflow.
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPWatchThreadInit() {
    if(!TVPWatchThread) {
        TVPWatchThread = new tTVPWatchThread();
    }
}

//---------------------------------------------------------------------------
static void TVPWatchThreadUninit() {
    if(TVPWatchThread) {
        delete TVPWatchThread;
        TVPWatchThread = nullptr;
    }
}

//---------------------------------------------------------------------------
static tTVPAtExit TVPWatchThreadUninitAtExit(TVP_ATEXIT_PRI_SHUTDOWN,
                                             TVPWatchThreadUninit);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetTickCount
//---------------------------------------------------------------------------
tjs_uint64 TVPGetTickCount() {
    TVPWatchThreadInit();

    DWORD curtick = TVPCheckTickOverflow();

    return curtick + TVPTickCountBias;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPStartTickCount
//---------------------------------------------------------------------------
void TVPStartTickCount() { TVPWatchThreadInit(); }
//---------------------------------------------------------------------------

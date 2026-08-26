#include "Thread.h"
#include <thread>
#include <stdexcept>
#include "MsgIntf.h"
#include "ThreadImpl.h"

NS_KRMOVIE_BEGIN

CThread::CThread() : m_bStop(false), m_bRunning(false) {}

CThread::~CThread() {
    if(m_bRunning) {
        StopThread();
    }
    if(m_ThreadId) {
        m_ThreadId->join();
        delete m_ThreadId;
    }
}

void CThread::Create() {
    if(m_bRunning.exchange(true)) {
        TVPThrowExceptionMessage(TJS_W("thread already in running"));
    }
    m_bStop = false;
    if(m_ThreadId) {
        m_ThreadId->join();
        delete m_ThreadId;
    }
    m_ThreadId = new std::thread(&CThread::entry, this);
}

void CThread::StopThread(bool bWait /*= true*/) {
    m_bStop = true;
    m_StopEvent.notify_all();
    // OnExit belongs to entry(), not StopThread.  A null m_ThreadId therefore
    // means that setting/notifying stop performs no derived-class cleanup.
    if(m_ThreadId && bWait) {
        m_ThreadId->join();
        delete m_ThreadId;
        m_ThreadId = nullptr;
    }
}

void CThread::Sleep(unsigned int milliseconds) {
    if(IsCurrentThread()) {
        std::unique_lock<std::mutex> lock(m_mtxStopEvent);
#ifdef __EMSCRIPTEN__
        // Platform boundary (emscripten): the futex emulation lets
        // pthread_cond_timedwait return EAGAIN when a notify races the
        // wait — semantically a normal wakeup. libc++'s noexcept
        // __do_timed_wait treats anything other than 0/ETIMEDOUT as fatal
        // and terminates the process (measured rc=EAGAIN here aborting
        // movie playback). Wait on the native handle and treat
        // EAGAIN/EINTR as spurious wakeups instead.
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += milliseconds / 1000;
        ts.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
        if(ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        int rc = pthread_cond_timedwait(m_StopEvent.native_handle(),
                                        lock.mutex()->native_handle(), &ts);
        if(rc != 0 && rc != ETIMEDOUT && rc != EAGAIN && rc != EINTR) {
            fprintf(stderr,
                    "CThread::Sleep(%u): pthread_cond_timedwait rc=%d\n",
                    milliseconds, rc);
        }
#else
        m_StopEvent.wait_for(lock, std::chrono::milliseconds(milliseconds));
#endif
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

bool CThread::IsCurrentThread() {
    if(!m_ThreadId)
        return false;
    return m_ThreadId->get_id() == std::this_thread::get_id();
}

int CThread::entry() {
    // OnExit completes before m_bRunning becomes false and before a joining
    // StopThread returns.  There is no equivalent sequence without Create().
    // There is also no exception boundary here: an exception escaping Process
    // (for example, allocation failure in a worker Ended callback) prevents
    // OnExit and escapes the std::thread entry, which terminates the process.
    OnStartup();
    Process();
    OnExit();
    m_bRunning = false;
    TVPOnThreadExited();
    return 0;
}

NS_KRMOVIE_END

#include "MessageQueue.h"
#include "Clock.h"
#include "DemuxPacket.h"
#include <cmath>

NS_KRMOVIE_BEGIN
CDVDMessageQueue::CDVDMessageQueue(std::string owner) :
    /*m_hEvent(true),*/ m_owner(owner) {
    // The reference constructor copy-constructs m_owner from its parameter.
    // It also intentionally leaves m_drain indeterminate until Init (or the
    // first WaitUntilEmpty); do not add a constructor initializer for it.
    m_iDataSize = 0;
    m_bAbortRequest = false;
    m_bInitialized = false;

    m_TimeBack = DVD_NOPTS_VALUE;
    m_TimeFront = DVD_NOPTS_VALUE;
    m_TimeSize = 1.0 / 4.0; /* 4 seconds */
    m_iMaxDataSize = 0;
}

CDVDMessageQueue::~CDVDMessageQueue() {
    // remove all remaining messages
    Flush(CDVDMsg::NONE);
}

void CDVDMessageQueue::Init() {
    // Init resets only bookkeeping.  It does not clear either
    // std::list, so messages queued on a never-started player survive a repeat
    // OpenFromStream and are older than newly queued messages.  Their eventual
    // effect is type-specific: speed messages are not coalesced, while a seek
    // is discarded if another seek/chapter request remains after it is popped.
    m_iDataSize = 0;
    m_bAbortRequest = false;
    m_bInitialized = true;
    m_TimeBack = DVD_NOPTS_VALUE;
    m_TimeFront = DVD_NOPTS_VALUE;
    m_drain = false;
}

void CDVDMessageQueue::Flush(CDVDMsg::Message type) {
    // m_section is recursive because End calls Flush while already holding it.
    // Both lists own message references through DVDMessageListItem; erasing a
    // node releases the message before deallocating the list node.
    CSingleLock lock(m_section);

    m_messages.remove_if([type](const DVDMessageListItem &item) {
        return type == CDVDMsg::NONE || item.message->IsType(type);
    });

    m_prioMessages.remove_if([type](const DVDMessageListItem &item) {
        return type == CDVDMsg::NONE || item.message->IsType(type);
    });

    if(type == CDVDMsg::DEMUXER_PACKET || type == CDVDMsg::NONE) {
        m_iDataSize = 0;
        m_TimeBack = DVD_NOPTS_VALUE;
        m_TimeFront = DVD_NOPTS_VALUE;
    }
}

void CDVDMessageQueue::Abort() {
    CSingleLock lock(m_section);

    m_bAbortRequest = true;

    // Notify while m_section is still held.  The condition variable uses the
    // distinct m_mtxEvent mutex, so this does not form a predicate+mutex pair
    // with Get and does not eliminate Get's pre-wait lost-wakeup window.
    m_hEvent.notify_all();
}

void CDVDMessageQueue::End() {
    CSingleLock lock(m_section);

    // NONE removes both normal and priority lists.  drain is not reset and no
    // waiter notification is sent here; the next Init resets drain.
    Flush(CDVDMsg::NONE);

    m_bInitialized = false;
    m_iDataSize = 0;
    m_bAbortRequest = false;
}

MsgQueueReturnCode CDVDMessageQueue::Put(CDVDMsg *pMsg, int priority,
                                         bool front) {
    CSingleLock lock(m_section);

    if(!m_bInitialized) {
        //	CLog::Log(LOGWARNING, "CDVDMessageQueue(%s)::Put
        // MSGQ_NOT_INITIALIZED", m_owner.c_str());
        // Put checks initialization before null: a null pointer on this path
        // is dereferenced and crashes.  A non-null Put consumes the supplied
        // ref even on this failure.  Child
        // SendMessage discards the -2 result, so its parent may still advance
        // a producer-side packet counter after this destroys the packet.
        pMsg->Release();
        return MSGQ_NOT_INITIALIZED;
    }
    if(!pMsg) {
        //	CLog::Log(LOGFATAL, "CDVDMessageQueue(%s)::Put
        // MSGQ_INVALID_MSG", m_owner.c_str());
        // Initialization is checked first; only the initialized-null case
        // reaches this -3 return, and there is no reference to release.
        return MSGQ_INVALID_MSG;
    }

    if(priority > 0) {
        // The priority list is ascending from begin to end and Get pops back.
        // front=true inserts before the existing equal-priority run, so old
        // equal-priority nodes are consumed first (FIFO).  front=false uses
        // priority+1 only for the search key, placing the new node after the
        // equal-priority run while storing the original priority (LIFO within
        // that run).  The compiled INT_MAX+1 search key wraps on all four ARM
        // targets even though signed overflow is not portable C++ behavior.
        int prio = priority;
        if(!front)
            prio++;

        auto it = std::find_if(m_prioMessages.begin(), m_prioMessages.end(),
                               [prio](const DVDMessageListItem &item) {
                                   return prio <= item.priority;
                               });
        m_prioMessages.emplace(it, pMsg, priority);
    } else {
        // Normal nodes use the opposite physical ends for insertion, while
        // Get always pops back: front=true is FIFO and front=false is LIFO.
        // Negative priorities are normal-list nodes, not priority-list nodes.
        if(front)
            m_messages.emplace_front(pMsg, priority);
        else
            m_messages.emplace_back(pMsg, priority);
    }

    // Only priority exactly zero participates in byte/timestamp accounting;
    // positive-priority and negative-priority packet messages remain nodes but
    // are invisible to m_iDataSize/m_TimeFront/m_TimeBack.
    if(pMsg->IsType(CDVDMsg::DEMUXER_PACKET) && priority == 0) {
        DemuxPacket *packet = ((CDVDMsgDemuxerPacket *)pMsg)->GetPacket();
        if(packet) {
            m_iDataSize += packet->iSize;
            if(packet->dts != DVD_NOPTS_VALUE)
                m_TimeFront = packet->dts;
            else if(packet->pts != DVD_NOPTS_VALUE)
                m_TimeFront = packet->pts;

            if(m_TimeBack == DVD_NOPTS_VALUE)
                m_TimeBack = m_TimeFront;
        }
    }

    pMsg->Release();

    // inform waiter for new packet
    m_hEvent.notify_all();

    return MSGQ_OK;
}

MsgQueueReturnCode CDVDMessageQueue::Get(CDVDMsg **pMsg,
                                         unsigned int iTimeoutInMilliSeconds,
                                         int &priority) {
    CSingleLock lock(m_section);

    // The output slot is cleared before initialization is checked.  Passing a
    // null pMsg therefore crashes even when the queue is uninitialized.
    *pMsg = nullptr;

    int ret = 0;

    if(!m_bInitialized) {
        //	CLog::Log(LOGFATAL, "CDVDMessageQueue(%s)::Get
        // MSGQ_NOT_INITIALIZED", m_owner.c_str());
        return MSGQ_NOT_INITIALIZED;
    }

    while(!m_bAbortRequest) {
        // A positive requested priority always selects m_prioMessages.  At a
        // non-positive threshold, any priority node still blocks selection of
        // the normal list.  m_drain bypasses only the back-node priority gate;
        // it does not change that list-selection rule.
        std::list<DVDMessageListItem> &msgs =
            (priority > 0 || !m_prioMessages.empty()) ? m_prioMessages
                                                      : m_messages;

        if(!msgs.empty() && (msgs.back().priority >= priority || m_drain)) {
            DVDMessageListItem &item(msgs.back());
            priority = item.priority;

            if(item.message->IsType(CDVDMsg::DEMUXER_PACKET) &&
               item.priority == 0) {
                DemuxPacket *packet =
                    ((CDVDMsgDemuxerPacket *)item.message)->GetPacket();
                if(packet) {
                    m_iDataSize -= packet->iSize;
                    if(packet->dts != DVD_NOPTS_VALUE)
                        m_TimeBack = packet->dts;
                    else if(packet->pts != DVD_NOPTS_VALUE)
                        m_TimeBack = packet->pts;
                }
            }

            *pMsg = item.message->AddRef();
            msgs.pop_back();

            ret = MSGQ_OK;
            break;
        } else if(!iTimeoutInMilliSeconds) {
            ret = MSGQ_TIMEOUT;
            break;
        } else {
            //			m_hEvent.Reset();
            m_section.unlock();

            // The queue predicate is protected by m_section, but waiting uses
            // the separate m_mtxEvent.  A Put/Abort notify between the unlock
            // above and the actual wait can be lost.  Every non-timeout wake
            // restarts the full relative timeout; End neither notifies nor
            // causes this loop to recheck m_bInitialized.  The CSingleLock
            // still records ownership while the raw recursive mutex is
            // temporarily unlocked, matching the reference exception path.
            std::unique_lock<std::mutex> eventLock(m_mtxEvent);
            if(m_hEvent.wait_for(
                   eventLock,
                   std::chrono::milliseconds(iTimeoutInMilliSeconds)) ==
               std::cv_status::timeout) {
                m_section.lock();
                return MSGQ_TIMEOUT;
            }

            m_section.lock();
        }
    }

    if(m_bAbortRequest)
        return MSGQ_ABORT;

    return (MsgQueueReturnCode)ret;
}

unsigned CDVDMessageQueue::GetPacketCount(CDVDMsg::Message type) {
    CSingleLock lock(m_section);

    if(!m_bInitialized)
        return 0;

    unsigned count = 0;
    for(const auto &item : m_messages) {
        if(item.message->IsType(type))
            count++;
    }
    for(const auto &item : m_prioMessages) {
        if(item.message->IsType(type))
            count++;
    }

    return count;
}

void CDVDMessageQueue::WaitUntilEmpty() {
    {
        CSingleLock lock(m_section);
        m_drain = true;
    }

    //	CLog::Log(LOGNOTICE, "CDVDMessageQueue(%s)::WaitUntilEmpty",
    // m_owner.c_str());
    // This is a FIFO barrier for normal messages already ahead of the marker,
    // not an atomic "queue is empty" observation: later front=true normal
    // messages remain behind it, while priority messages and front=false
    // normal messages can still run first.  drain lets consumers pass their
    // priority threshold but does not alter priority-list precedence.
    CDVDMsgGeneralSynchronize *msg =
        new CDVDMsgGeneralSynchronize(40000, SYNCSOURCE_ANY);
    // Put's return and Wait's result are deliberately ignored.  On a normal
    // path the temporary AddRef is consumed by Put and the list node holds its
    // own AddRef.  If allocation/insertion/wait throws, there is no scope guard
    // for msg or m_drain, so references can leak and drain can remain true.
    Put(msg->AddRef());
    msg->Wait(m_bAbortRequest, 0);
    msg->Release();

    {
        CSingleLock lock(m_section);
        m_drain = false;
    }
}

int CDVDMessageQueue::GetLevel() {
    CSingleLock lock(m_section);

    // The fast-full comparison is strict.  At dataSize == maxDataSize a
    // timestamp-based queue can still report less than 100.
    if(m_iDataSize > m_iMaxDataSize)
        return 100;
    if(m_iDataSize == 0)
        return 0;

    if(IsDataBased())
        return std::min(100, 100 * m_iDataSize / m_iMaxDataSize);

    int level = std::min(100.0,
                         std::ceil(100.0 * m_TimeSize *
                                   (m_TimeFront - m_TimeBack) / DVD_TIME_BASE));

    // if we added lots of packets with NOPTS, make sure that the
    // queue is not signalled empty
    if(level == 0 && m_iDataSize != 0) {
        // Preserve the non-empty minimum; AcceptsData only rejects level 100.
        //	CLog::Log(LOGDEBUG, "CDVDMessageQueue::GetLevel() - can't
        // determine level");
        return 1;
    }

    return level;
}

void CDVDMessageQueue::SetMaxTimeSize(double sec) {
    // <=1, negative infinity and NaN all select 1.0; +infinity stores 0.0.
    m_TimeSize = 1.0 / std::max(1.0, sec);
}

int CDVDMessageQueue::GetTimeSize() {
    CSingleLock lock(m_section);

    if(IsDataBased())
        return 0;
    else
        return (int)((m_TimeFront - m_TimeBack) / DVD_TIME_BASE);
}

bool CDVDMessageQueue::IsDataBased() const {
    return (m_TimeBack == DVD_NOPTS_VALUE || m_TimeFront == DVD_NOPTS_VALUE ||
            m_TimeFront <= m_TimeBack);
}
NS_KRMOVIE_END

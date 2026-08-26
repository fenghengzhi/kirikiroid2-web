#include "BitstreamStats.h"
#include "TimeUtils.h"

NS_KRMOVIE_BEGIN
int64_t BitstreamStats::m_tmFreq;

BitstreamStats::BitstreamStats(unsigned int nEstimatedBitrate) {
    m_dBitrate = 0.0;
    m_dMaxBitrate = 0.0;
    m_dMinBitrate = -1.0;

    m_nBitCount = 0;
    m_nEstimatedBitrate = nEstimatedBitrate;
    m_tmStart = 0LL;

    // This process-wide lazy initialization is deliberately non-atomic.  The
    // two embedded audio/video instances normally see the same 1000 Hz value,
    // but the reference constructor has no once-guard or locking layer.
    if(m_tmFreq == 0LL)
        m_tmFreq = CurrentHostFrequency();
}

BitstreamStats::~BitstreamStats() = default;

void BitstreamStats::AddSampleBytes(unsigned int nBytes) {
    // Keep both the multiply and the following accumulation in unsigned
    // 32-bit arithmetic.  The reference code neither widens nor clamps either
    // overflow boundary.
    AddSampleBits(nBytes * 8);
}

void BitstreamStats::AddSampleBits(unsigned int nBits) {
    m_nBitCount += nBits;
    // CalculateBitrate deliberately leaves the accumulated count untouched
    // while less than two seconds have elapsed, so every later sample keeps
    // re-entering the threshold path until an update is finally published.
    if(m_nBitCount >= m_nEstimatedBitrate)
        CalculateBitrate();
}

void BitstreamStats::Start() {
    // CalculateBitrate calls this helper instead of reusing its earlier
    // tmNow.  Preserve the second host-counter read after clearing the count.
    m_nBitCount = 0;
    m_tmStart = CurrentHostCounter();
}

void BitstreamStats::CalculateBitrate() {
    int64_t tmNow;
    tmNow = CurrentHostCounter();

    // CurrentHostCounter zero-extends a wrapping 32-bit rough millisecond
    // tick.  A wrap can therefore make this signed delta negative and keep the
    // two-second gate closed until the new epoch catches the previous start.
    double elapsed = (double)(tmNow - m_tmStart) / (double)m_tmFreq;
    // only update once every 2 seconds
    if(elapsed >= 2) {
        m_dBitrate = (double)m_nBitCount / elapsed;

        if(m_dBitrate > m_dMaxBitrate)
            m_dMaxBitrate = m_dBitrate;

        if(m_dBitrate < m_dMinBitrate || m_dMinBitrate == -1)
            m_dMinBitrate = m_dBitrate;

        Start();
    }
}

NS_KRMOVIE_END

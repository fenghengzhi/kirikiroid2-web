#include "Clock.h"
#include "MathUtils.h"
#include "TimeUtils.h"

NS_KRMOVIE_BEGIN

CDVDClock::CDVDClock() {
    CSingleLock lock(m_systemsection);

    m_pauseClock = 0;
    m_bReset = true;
    m_paused = false;
    m_iDisc = 0;
    m_maxspeedadjust = 0.0;
    m_systemAdjust = 0;
    m_speedAdjust = 0;
    m_startClock = 0;
    m_vSyncAdjust = 0;
    m_frameTime = DVD_TIME_BASE / 60.0;

    m_videoRefClock.reset(new CVideoReferenceClock());
    m_lastSystemTime = m_videoRefClock->GetTime();
    m_systemOffset = m_videoRefClock->GetTime();
    m_systemFrequency = (int64_t)CurrentHostFrequency();
    m_systemUsed = m_systemFrequency;
}

CDVDClock::~CDVDClock() = default;

// Returns the current absolute clock in units of DVD_TIME_BASE
// (usually microseconds).
double CDVDClock::GetAbsoluteClock(bool interpolated /*= true*/) {
    CSingleLock lock(m_systemsection);

    int64_t current;
    current = m_videoRefClock->GetTime(interpolated);

    return SystemToAbsolute(current);
}

double CDVDClock::GetClock(bool interpolated /*= true*/) {
    CSingleLock lock(m_critSection);

    int64_t current = m_videoRefClock->GetTime(interpolated);
    m_systemAdjust += m_speedAdjust * (current - m_lastSystemTime);
    m_lastSystemTime = current;

    return SystemToPlaying(current);
}

double CDVDClock::GetClock(double &absolute, bool interpolated /*= true*/) {
    // This overload samples before locking and then uses m_systemsection, not
    // the m_critSection used by the scalar overload.  Both paths eventually
    // mutate the same playing-clock fields through SystemToPlaying().
    int64_t current = m_videoRefClock->GetTime(interpolated);

    CSingleLock lock(m_systemsection);
    // The caller-visible absolute value is published before either internal
    // accumulator.  The following compound assignment converts through double
    // and then truncates back to signed int64_t without a finite/range guard.
    absolute = SystemToAbsolute(current);

    m_systemAdjust += m_speedAdjust * (current - m_lastSystemTime);
    m_lastSystemTime = current;

    return SystemToPlaying(current);
}

void CDVDClock::SetVsyncAdjust(double adjustment) {
    // Publish the raw double under the primary clock lock.  SystemToPlaying's
    // reset path can also clear this field while the absolute-output GetClock
    // holds the distinct system lock, so this is deliberately not one global
    // field generation and the value is not normalized or range-checked.
    CSingleLock lock(m_critSection);
    m_vSyncAdjust = adjustment;
}

double CDVDClock::GetVsyncAdjust() {
    CSingleLock lock(m_critSection);
    return m_vSyncAdjust;
}

void CDVDClock::Pause(bool pause) {
    CSingleLock lock(m_critSection);

    if(pause && !m_paused) {
        if(!m_pauseClock)
            m_speedAfterPause =
                m_systemFrequency * DVD_PLAYSPEED_NORMAL / m_systemUsed;
        else
            m_speedAfterPause = DVD_PLAYSPEED_PAUSE;

        SetSpeed(DVD_PLAYSPEED_PAUSE);
        m_paused = true;
    } else if(!pause && m_paused) {
        m_paused = false;
        SetSpeed(m_speedAfterPause);
    }
}

void CDVDClock::SetSpeed(int iSpeed) {
    // this will sometimes be a little bit of due to rounding errors,
    // ie clock might jump abit when changing speed
    CSingleLock lock(m_critSection);

    if(m_paused) {
        m_speedAfterPause = iSpeed;
        return;
    }

    if(iSpeed == DVD_PLAYSPEED_PAUSE) {
        // Repeated pause requests do not advance the frozen clock snapshot.
        if(!m_pauseClock)
            m_pauseClock = m_videoRefClock->GetTime();
        return;
    }

    // Signed integer division is intentional and also covers reverse speeds.
    int64_t current;
    int64_t newfreq = m_systemFrequency * DVD_PLAYSPEED_NORMAL / iSpeed;

    current = m_videoRefClock->GetTime();
    if(m_pauseClock) {
        m_startClock += current - m_pauseClock;
        m_pauseClock = 0;
    }

    m_startClock = current -
        (int64_t)((double)(current - m_startClock) * newfreq / m_systemUsed);
    m_systemUsed = newfreq;
}

void CDVDClock::SetSpeedAdjust(double adjust) {
    //	CLog::Log(LOGDEBUG, "CDVDClock::SetSpeedAdjust - adjusted:%f",
    // adjust);

    CSingleLock lock(m_critSection);
    // SetCaching publishes its new cache state before making this reset call.
    m_speedAdjust = adjust;
}

double CDVDClock::GetSpeedAdjust() {
    CSingleLock lock(m_critSection);
    return m_speedAdjust;
}

double CDVDClock::ErrorAdjust(double error, const char *log) {
    // This outer recursive lock remains held while GetClock takes the distinct
    // system section and while Discontinuity recursively takes this lock again.
    CSingleLock lock(m_critSection);

    double clock, absolute, adjustment;
    // Sampling and accumulator/reset publication happen even when a later gate
    // returns zero.  The optimized Android/iOS bodies never read log.
    clock = GetClock(absolute);

    // skip minor updates while speed adjust is active
    // -> adjusting buffer levels
    if(m_speedAdjust != 0 && error < DVD_MSEC_TO_TIME(100)) {
        return 0;
    }

    adjustment = error;

    if(m_vSyncAdjust != 0) {
        // Audio ahead is more noticeable then audio behind video.
        // Correct if aufio is more than 20ms ahead or more then
        // 27ms behind. In a worst case scenario we switch from
        // 20ms ahead to 21ms behind (for fps of 23.976)
        if(error > 0.02 * DVD_TIME_BASE)
            adjustment = m_frameTime;
        else if(error < -0.027 * DVD_TIME_BASE)
            adjustment = -m_frameTime;
        else
            adjustment = 0;
    }

    if(adjustment == 0)
        return 0;

    Discontinuity(clock + adjustment, absolute);

    // 	CLog::Log(LOGDEBUG, "CDVDClock::ErrorAdjust - %s - error:%f,
    // adjusted:%f", 		log, error, adjustment);
    return adjustment;
}

void CDVDClock::Discontinuity(double clock, double absolute) {
    CSingleLock lock(m_critSection);
    m_startClock = AbsoluteToSystem(absolute);
    if(m_pauseClock)
        m_pauseClock = m_startClock;
    m_iDisc = clock;
    m_bReset = false;
    m_systemAdjust = 0;
    m_speedAdjust = 0;
}

void CDVDClock::SetMaxSpeedAdjust(double speed) {
    // The four references store the raw double under this dedicated recursive
    // mutex without validating sign or finiteness. On 32-bit targets the two
    // word stores both occur while the lock is held.
    CSingleLock lock(m_speedsection);

    m_maxspeedadjust = speed;
}

// returns the refreshrate if the videoreferenceclock is running, -1
// otherwise
int CDVDClock::UpdateFramerate(double fps, double *interval /*= nullptr*/) {
    // sent with fps of 0 means we are not playing video
    if(fps == 0.0)
        return -1;

    m_frameTime = 1 / fps * DVD_TIME_BASE;

    // check if the videoreferenceclock is running, will return -1 if
    // not
    double rate = m_videoRefClock->GetRefreshRate(interval);

    if(rate <= 0)
        return -1;

    double speed;
    {
        CSingleLock lock(m_speedsection);

        double weight =
            MathUtils::round_int(rate) / (double)MathUtils::round_int(fps);

        // set the speed of the videoreferenceclock based on fps,
        // refreshrate and maximum speed adjust set by user
        if(m_maxspeedadjust > 0.05) {
            if(weight / MathUtils::round_int(weight) <
                   1.0 + m_maxspeedadjust / 100.0 &&
               weight / MathUtils::round_int(weight) >
                   1.0 - m_maxspeedadjust / 100.0)
                weight = MathUtils::round_int(weight);
        }
        speed = rate / (fps * weight);
    }

    m_videoRefClock->SetSpeed(speed);

    return rate;
}

bool CDVDClock::GetClockInfo(int &MissedVblanks, double &ClockSpeed,
                             double &RefreshRate) const {
    return m_videoRefClock->GetClockInfo(MissedVblanks, ClockSpeed,
                                         RefreshRate);
}

double CDVDClock::SystemToAbsolute(int64_t system) {
    return DVD_TIME_BASE * (double)(system - m_systemOffset) /
        m_systemFrequency;
}

int64_t CDVDClock::AbsoluteToSystem(double absolute) {
    // Preserve divide -> multiply -> signed-int64 conversion -> offset addition.
    // There is no lock, finite/range guard or alternate saturating helper.
    return (int64_t)(absolute / DVD_TIME_BASE * m_systemFrequency) +
        m_systemOffset;
}

double CDVDClock::SystemToPlaying(int64_t system) {
    // This helper deliberately has no lock of its own.  The scalar GetClock
    // calls it under m_critSection, while the absolute-output overload calls it
    // under m_systemsection, so the two APIs do not form one lock generation.
    int64_t current;

    if(m_bReset) {
        m_startClock = system;
        m_systemUsed = m_systemFrequency;
        if(m_pauseClock)
            m_pauseClock = m_startClock;
        m_iDisc = 0;
        m_systemAdjust = 0;
        m_speedAdjust = 0;
        m_vSyncAdjust = 0;
        m_bReset = false;
    }

    // The frozen-clock decision uses the zero-sentinel pauseClock itself, not
    // m_paused.  The final signed tick arithmetic/division has no zero or
    // overflow guard.
    if(m_pauseClock)
        current = m_pauseClock;
    else
        current = system;

    return DVD_TIME_BASE * (double)(current - m_startClock + m_systemAdjust) /
        m_systemUsed +
        m_iDisc;
}

double CDVDClock::GetClockSpeed() {
    CSingleLock lock(m_critSection);

    double speed = (double)m_systemFrequency / m_systemUsed;
    return m_videoRefClock->GetSpeed() * speed + m_speedAdjust;
}

NS_KRMOVIE_END

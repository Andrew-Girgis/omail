#pragma once
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>

// The one source of "now" for everything Omail shows, stores, or schedules. It is the
// system clock unless pinned. Repro runs pin it (OMAIL_NOW) so dates, "today", and demo
// data come out the same on every run. A pinned clock still advances in real time from
// its start, so timers, the scheduler's pause detection, and retry delays keep working.
// OAuth token expiry stays on the system clock: providers issue it in real time.
namespace Clock {
struct State {
    QDateTime start;
    QElapsedTimer elapsed;
};
inline State &state() {
    static State s;
    return s;
}
inline void pin(const QDateTime &start) {
    state().start = start.toUTC();
    state().elapsed.start();
}
inline void unpin() { state().start = {}; }
inline bool pinned() { return state().start.isValid(); }
inline QDateTime nowUtc() {
    const auto &s = state();
    return s.start.isValid() ? s.start.addMSecs(s.elapsed.elapsed())
                             : QDateTime::currentDateTimeUtc();
}
// The local calendar date, as QDate::currentDate() would give it.
inline QDate today() { return nowUtc().toLocalTime().date(); }
} // namespace Clock

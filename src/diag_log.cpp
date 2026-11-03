// The session's diagnostics (diag_log.hpp).
#include "spidy/diag_log.hpp"
#include <cstdarg>
#include <windows.h>
#ifndef SPIDY_VERSION
#define SPIDY_VERSION ""
#endif
#ifndef SPIDY_BUILD_COMMIT
#define SPIDY_BUILD_COMMIT ""
#endif
#ifndef SPIDY_BUILD_STAMP
#define SPIDY_BUILD_STAMP __DATE__ " " __TIME__
#endif
using namespace spidy;

namespace {
SRWLOCK lock = SRWLOCK_INIT;
diag_log::Ring<diag_log::mainLines> mainRing;
diag_log::Ring<diag_log::aimLines> aimRing;
ULONGLONG firstTick{};
} // namespace

void diag_log::write(const char* text) {
    if (!text)
        return;
    char line[lineChars];
    AcquireSRWLockExclusive(&lock);
    const ULONGLONG now = GetTickCount64();
    if (!firstTick)
        firstTick = now;
    const double seconds = static_cast<double>(now - firstTick) / 1000.0;
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%8.3f] %s", seconds, text);
    if (channelOf(text) == Channel::aim)
        aimRing.push(line);
    else
        mainRing.push(line);
    ReleaseSRWLockExclusive(&lock);
}

void diag_log::writef(const char* format, ...) {
    char text[lineChars];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    write(text);
}

void diag_log::copyOut(Data& out) {
    AcquireSRWLockShared(&lock);
    if (out.main.written != mainRing.written)
        out.main = mainRing;
    if (out.aim.written != aimRing.written)
        out.aim = aimRing;
    ReleaseSRWLockShared(&lock);
}

const char* diag_log::build() {
    static const std::string id = buildId(SPIDY_VERSION, SPIDY_BUILD_COMMIT, SPIDY_BUILD_STAMP);
    return id.c_str();
}

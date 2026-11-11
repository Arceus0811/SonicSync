#pragma once

#define NOMINMAX
#include <windows.h>

namespace sonicsync::win {

// A tiny, bounded UI-to-render control block. Each field is transferred with
// Interlocked operations, so the audio thread never waits for the UI process.
struct ControlBlock {
    // Single UI writer. All payload words are atomic across process boundaries.
    volatile LONG version;
    volatile LONG generation;
    struct Slot {
        volatile LONG profile, bypass, preamp;
        volatile LONG gains[10];
    } slots[2];
    volatile LONG sampleRate;
    volatile LONG inputPeakMilli;
    volatile LONG outputPeakMilli;
    volatile LONG routeStatus; // 0=starting/stopped, 1=clients started/waiting for audio, 2=audio processed
    volatile LONG routeError;  // HRESULT from the child route when it exits unexpectedly
    volatile LONG spectrumMilli[16];
};

inline LONG load(volatile LONG* value) noexcept {
    return InterlockedCompareExchange(value, 0, 0);
}

inline void store(volatile LONG* value, LONG next) noexcept {
    InterlockedExchange(value, next);
}

} // namespace sonicsync::win

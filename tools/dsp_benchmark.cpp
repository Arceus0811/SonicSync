#include "eq_state.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using clock = std::chrono::steady_clock;
    constexpr std::array<double, 4> rates{44100.0, 48000.0, 96000.0, 192000.0};
    constexpr std::array<std::size_t, 4> blockSizes{128, 256, 480, 960};
    constexpr double audioSeconds = 4.0;
    constexpr double pi = 3.14159265358979323846;
    std::cout << "DSP benchmark (stereo, ten active bands, limiter and crossfeed enabled)\n"
                 "rate_hz,block_frames,realtime_percent,realtime_factor\n";
    for (const double rate : rates) {
        for (const std::size_t blockFrames : blockSizes) {
            sonicsync::Processor processor;
            if (!processor.prepare(rate, 2)) return 1;
            sonicsync::EqState state;
            for(int i=0;i<10;++i) state.setBand(i, i%2 ? -2000 : 2000);
            processor.setPresetRealtime(sonicsync::makeEqPreset(state,rate));
            processor.setCrossfeed(true);
            const std::size_t samples = blockFrames * 2;
            std::vector<float> block(samples);
            for (std::size_t frame = 0; frame < blockFrames; ++frame) {
                const float x = 0.05f * static_cast<float>(std::sin(2.0 * pi * 110.0 * frame / rate)) +
                                0.02f * static_cast<float>(std::sin(2.0 * pi * 1100.0 * frame / rate));
                block[2 * frame] = x;
                block[2 * frame + 1] = x;
            }
            const auto calls = static_cast<std::size_t>(rate * audioSeconds) / blockFrames;
            const auto start = clock::now();
            for (std::size_t call = 0; call < calls; ++call)
                processor.process(block.data(), blockFrames);
            const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
            const double audioTime = static_cast<double>(calls * blockFrames) / rate;
            const double realtime = elapsed / audioTime;
            std::cout << std::fixed << std::setprecision(3) << rate << ',' << blockFrames << ','
                      << realtime * 100.0 << ',' << (realtime > 0.0 ? 1.0 / realtime : 0.0) << '\n';
        }
    }
}

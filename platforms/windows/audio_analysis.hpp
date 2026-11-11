#pragma once

#include "control_mapping.hpp"
#include "dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sonicsync::win {

inline void publishAnalysis(const float* samples, std::size_t frames, std::size_t channels,
                            const std::array<float, 16>& coefficients,
                            const Processor& processor, ControlBlock* controls) noexcept {
    if (!controls || !samples || frames == 0 || (channels < 1 || channels > Processor::kMaxChannels)) return;
    const auto input = processor.inputMeter();
    const auto output = processor.outputMeter();
    store(&controls->inputPeakMilli, static_cast<LONG>(std::clamp(input.peak, 0.0f, 2.0f) * 500.0f));
    store(&controls->outputPeakMilli, static_cast<LONG>(std::clamp(output.peak, 0.0f, 2.0f) * 500.0f));

    for (std::size_t band = 0; band < coefficients.size(); ++band) {
        double q1 = 0.0, q2 = 0.0;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float mono = channels == 2
                ? 0.5f * (samples[frame * 2] + samples[frame * 2 + 1])
                : samples[frame*channels];
            const double q0 = mono + static_cast<double>(coefficients[band]) * q1 - q2;
            q2 = q1;
            q1 = q0;
        }
        const double power = std::max(0.0, q1 * q1 + q2 * q2 -
                                      static_cast<double>(coefficients[band]) * q1 * q2);
        const double magnitude = std::sqrt(power) * 2.0 / static_cast<double>(frames);
        const double db = 20.0 * std::log10(std::max(magnitude, 1.0e-5));
        store(&controls->spectrumMilli[band], static_cast<LONG>(std::clamp((db + 80.0) * 12.5, 0.0, 1000.0)));
    }
}

} // namespace sonicsync::win

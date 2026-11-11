#pragma once
#include "dsp.hpp"

namespace sonicsync {
struct CatalogEntry {
    std::string_view id, name;
    std::array<double,10> gains, response;
};
#include "eq_catalog.inc"

// Control values are integer millidecibels, including on disk and across processes.
struct EqState {
    static constexpr int custom = -1;
    int profile{};
    std::array<int,10> bands{};
    int trim{};
    std::array<int,3> tone{};
    bool bypass{};

    void select(int index) noexcept;
    void setBand(int band, int milliDb) noexcept;
    void setTone(int group, int milliDb) noexcept;
    int effective(int band) const noexcept;
    int toneValue(int group) const noexcept;
    bool operator==(const EqState&) const = default;
};
Preset makeEqPreset(const EqState& state, double sampleRate);
std::string serializeEq(const EqState& state);
bool deserializeEq(std::string_view text, EqState& state);
void setGraphicBand(Band& band, int index, double gain, double sampleRate) noexcept;
double peakResponseDb(const Preset& preset, double sampleRate) noexcept;
}

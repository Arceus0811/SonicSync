#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sonicsync {

enum class FilterType : std::uint8_t { Peaking, LowShelf, HighShelf, HighPass, LowPass };

struct Band {
    FilterType type{FilterType::Peaking};
    double frequency{1000.0};
    double gainDb{0.0};
    double q{0.707};
    bool enabled{true};
};

struct Preset {
    std::string name{"Neutral"};
    double preampDb{0.0};
    double outputDb{0.0};
    std::array<Band, 10> bands{};
};

struct Meter { float peak{0}; float rms{0}; bool clipped{false}; };

class Biquad {
public:
    void configure(const Band& band, double sampleRate) noexcept;
    float process(float input) noexcept;
    void reset() noexcept;
    void flushDenormals() noexcept;
private:
    double b0_{1}, b1_{0}, b2_{0}, a1_{0}, a2_{0};
    double z1_{0}, z2_{0};
};

class Processor {
public:
    static constexpr std::size_t kMaxBands = 10;
    static constexpr std::size_t kMaxChannels = 16;
    bool dry() const noexcept { return bypass_ && bypassRamp_ == 0; }
    bool prepare(double sampleRate, std::size_t channels) noexcept;
    // Control methods and process() must be called from the same thread. In
    // particular, setPreset() reconfigures filter state and is not safe to call
    // concurrently with process(). Use setPresetRealtime() from the render thread.
    void setPreset(const Preset& preset);
    // Copies fixed-size numeric parameters only; it does not copy or allocate
    // the Preset::name string. Call on the same thread as process(). The name
    // returned by preset() is not updated by this method.
    void setPresetRealtime(const Preset& preset) noexcept;
    void setLimiter(bool enabled, float ceilingDb = -1.0f) noexcept;
    void setCrossfeed(bool enabled, float amount = 0.15f) noexcept;
    void setBypass(bool bypass) noexcept;
    void process(float* interleaved, std::size_t frames) noexcept;
    const Meter& inputMeter() const noexcept { return inputMeter_; }
    const Meter& outputMeter() const noexcept { return outputMeter_; }
    const Preset& preset() const noexcept { return preset_; }
private:
    void configureFilters(std::size_t bank) noexcept;
    std::array<std::array<std::array<Biquad, kMaxBands>, kMaxChannels>, 2> filters_{};
    std::array<std::array<bool, kMaxBands>, 2> filterEnabled_{};
    Preset preset_{};
    double sampleRate_{48000};
    std::size_t channels_{2};
    float gain_{1}, targetGain_{1}, gainStep_{0};
    std::uint32_t gainRamp_{0}, bypassRamp_{0};
    std::uint32_t filterRamp_{0};
    bool filterUpdatePending_{false};
    std::size_t activeFilterBank_{0};
    bool limiterEnabled_{true}, crossfeedEnabled_{false}, bypass_{false};
    float bypassMix_{0}, bypassStep_{0};
    float limiterCeiling_{0.8912509f}, crossfeedAmount_{0.15f};
    float limiterGain_{1.0f};
    float limiterReleaseCoeff_{0.9995834f};
    std::array<float, 2> crossfeedState_{};
    Meter inputMeter_{}, outputMeter_{};
};

std::string serializePreset(const Preset& preset);
bool deserializePreset(std::string_view text, Preset& out) noexcept;
double responseDb(const Preset& preset, double frequency, double sampleRate) noexcept;
Preset builtInPreset(std::string_view id);

} // namespace sonicsync

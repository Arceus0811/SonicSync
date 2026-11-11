#include "eq_state.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

namespace sonicsync {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinFreq = 10.0;
constexpr double kMinQ = 0.1;
constexpr double kMaxQ = 20.0;
float dbToGain(double db) noexcept { return static_cast<float>(std::pow(10.0, db / 20.0)); }

struct Coeff { double b0, b1, b2, a0, a1, a2; };
Coeff design(const Band& band, double sr) noexcept {
    const double f = std::clamp(band.frequency, kMinFreq, sr * 0.45);
    const double q = std::clamp(band.q, kMinQ, kMaxQ);
    const double w = 2.0 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
    const double A = std::pow(10.0, std::clamp(band.gainDb, -36.0, 24.0) / 40.0);
    const double alpha = s / (2.0 * q);
    Coeff x{1, 0, 0, 1, 0, 0};
    switch (band.type) {
    case FilterType::Peaking:
        x = {1 + alpha*A, -2*c, 1 - alpha*A, 1 + alpha/A, -2*c, 1 - alpha/A}; break;
    case FilterType::LowShelf: {
        const double beta = 2 * std::sqrt(A) * alpha;
        x = {A*((A+1)-(A-1)*c+beta), 2*A*((A-1)-(A+1)*c), A*((A+1)-(A-1)*c-beta),
             (A+1)+(A-1)*c+beta, -2*((A-1)+(A+1)*c), (A+1)+(A-1)*c-beta}; break; }
    case FilterType::HighShelf: {
        const double beta = 2 * std::sqrt(A) * alpha;
        x = {A*((A+1)+(A-1)*c+beta), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-beta),
             (A+1)-(A-1)*c+beta, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-beta}; break; }
    case FilterType::HighPass:
        x = {(1+c)/2, -(1+c), (1+c)/2, 1+alpha, -2*c, 1-alpha}; break;
    case FilterType::LowPass:
        x = {(1-c)/2, 1-c, (1-c)/2, 1+alpha, -2*c, 1-alpha}; break;
    }
    return x;
}
bool finiteBand(const Band& b, double sr) noexcept {
    const auto type = static_cast<unsigned>(b.type);
    return type <= static_cast<unsigned>(FilterType::LowPass) &&
        std::isfinite(b.frequency) && std::isfinite(b.gainDb) && std::isfinite(b.q) &&
        b.frequency >= kMinFreq && b.frequency < sr * 0.5 && b.gainDb >= -36 && b.gainDb <= 24 && b.q >= kMinQ && b.q <= kMaxQ;
}
bool parseDoubleLine(const std::string& text, double& value) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> value;
    if (!stream) return false;
    stream >> std::ws;
    return stream.eof();
}
} // namespace

void Biquad::configure(const Band& band, double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000) return;
    const auto c = design(band, sampleRate);
    b0_ = c.b0 / c.a0; b1_ = c.b1 / c.a0; b2_ = c.b2 / c.a0;
    a1_ = c.a1 / c.a0; a2_ = c.a2 / c.a0;
}
float Biquad::process(float x) noexcept {
    const double y = b0_*x + z1_;
    z1_ = b1_*x - a1_*y + z2_;
    z2_ = b2_*x - a2_*y;
    return static_cast<float>(y);
}
void Biquad::flushDenormals() noexcept {
    if (std::abs(z1_)<1e-24) z1_=0;
    if (std::abs(z2_)<1e-24) z2_=0;
}
void Biquad::reset() noexcept { z1_ = z2_ = 0; }

bool Processor::prepare(double sr, std::size_t channels) noexcept {
    if (!std::isfinite(sr) || sr < 8000 || sr > 384000 || (channels < 1 || channels > kMaxChannels)) return false;
    sampleRate_ = sr; channels_ = channels; activeFilterBank_ = 0; configureFilters(0); configureFilters(1);
    for (auto& bank : filters_) for (auto& c : bank) for (auto& f : c) f.reset();
    gain_ = dbToGain(preset_.preampDb + preset_.outputDb); targetGain_ = gain_; gainRamp_ = 0; filterRamp_ = 0;
    filterUpdatePending_ = false;
    bypassMix_ = bypass_ ? 1.0f : 0.0f; bypassRamp_ = 0; bypassStep_ = 0.0f;
    limiterGain_ = 1.0f;
    limiterReleaseCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate_ * 0.05)));
    inputMeter_ = {}; outputMeter_ = {}; crossfeedState_ = {};
    return true;
}
void Processor::configureFilters(std::size_t bank) noexcept {
    for (std::size_t ch=0; ch<channels_; ++ch) for (std::size_t i=0; i<kMaxBands; ++i) {
        Band b = preset_.bands[i]; if (!b.enabled) b = Band{FilterType::Peaking, 1000, 0, 0.707, true};
        filters_[bank][ch][i].configure(b, sampleRate_);
        if (bank != activeFilterBank_) filters_[bank][ch][i].reset();
        if(ch==0) filterEnabled_[bank][i] = b.enabled && preset_.bands[i].enabled &&
            b.frequency < sampleRate_*0.45 &&
            (b.gainDb!=0 || b.type==FilterType::HighPass || b.type==FilterType::LowPass);
    }
}
void Processor::setPreset(const Preset& p) {
    preset_ = p;
    setPresetRealtime(p);
}
void Processor::setPresetRealtime(const Preset& p) noexcept {
    // Keep the owned string unchanged: this method transfers fixed-size data
    // only and is safe to call at a render-buffer boundary.
    preset_.preampDb = p.preampDb;
    preset_.outputDb = p.outputDb;
    preset_.bands = p.bands;
    for (auto& b : preset_.bands) if (!finiteBand(b, sampleRate_)) b = Band{};
    preset_.preampDb = std::clamp(std::isfinite(p.preampDb) ? p.preampDb : 0.0, -36.0, 24.0);
    preset_.outputDb = std::clamp(std::isfinite(p.outputDb) ? p.outputDb : 0.0, -36.0, 12.0);
    // Both banks are audible during a fade. Apply the latest slider position
    // once the old bank is silent, so dragging never resets an audible filter.
    if (filterRamp_) {
        filterUpdatePending_ = true;
    } else {
        configureFilters(1 - activeFilterBank_);
        filterRamp_ = static_cast<std::uint32_t>(sampleRate_ * 0.020);
    }
    targetGain_ = dbToGain(preset_.preampDb + preset_.outputDb);
    gainRamp_ = static_cast<std::uint32_t>(std::max(1.0, sampleRate_ * 0.020));
    gainStep_ = (targetGain_ - gain_) / static_cast<float>(gainRamp_);
}
void Processor::setLimiter(bool enabled, float ceilingDb) noexcept {
    limiterEnabled_ = enabled;
    limiterCeiling_ = dbToGain(std::clamp(std::isfinite(ceilingDb) ? ceilingDb : -1.0f, -12.0f, -0.1f));
}
void Processor::setCrossfeed(bool enabled, float amount) noexcept {
    crossfeedEnabled_ = enabled; crossfeedAmount_ = std::clamp(std::isfinite(amount) ? amount : 0.15f, 0.0f, 0.5f);
}
void Processor::setBypass(bool bypass) noexcept {
    if (bypass_ == bypass) return;
    bypass_ = bypass;
    const float target = bypass ? 1.0f : 0.0f;
    bypassRamp_ = static_cast<std::uint32_t>(std::max(1.0, sampleRate_ * 0.015));
    bypassStep_ = (target - bypassMix_) / static_cast<float>(bypassRamp_);
}
void Processor::process(float* data, std::size_t frames) noexcept {
    if (!data || frames == 0) return;
    float inPeak=0, outPeak=0; double inSq=0, outSq=0; bool clipped=false;
    const std::size_t samples = frames * channels_;
    for (std::size_t n=0; n<frames; ++n) {
        float x[kMaxChannels];
        std::copy_n(data+n*channels_,channels_,x);
        for (std::size_t ch=0; ch<channels_; ++ch) {
            if (!std::isfinite(x[ch])) { x[ch] = 0.0f; clipped = true; }
            x[ch] = std::clamp(x[ch], -16.0f, 16.0f);
            inPeak=std::max(inPeak,std::abs(x[ch])); inSq+=static_cast<double>(x[ch])*x[ch]; if(inPeak>=1.0f) clipped=true;
        }
        const float* original=data+n*channels_;
        {
            const std::size_t nextBank = 1 - activeFilterBank_;
            const float mix = filterRamp_ ? 1.0f - static_cast<float>(filterRamp_) / static_cast<float>(std::max(1.0, sampleRate_ * 0.020)) : 0.0f;
            for (std::size_t ch=0; ch<channels_; ++ch) {
                float oldValue=x[ch], newValue=x[ch];
                for (std::size_t i=0; i<kMaxBands; ++i) {
                    if (filterEnabled_[activeFilterBank_][i]) oldValue=filters_[activeFilterBank_][ch][i].process(oldValue);
                    if (filterRamp_ && filterEnabled_[nextBank][i]) newValue=filters_[nextBank][ch][i].process(newValue);
                }
                x[ch] = filterRamp_ ? oldValue*(1.0f-mix)+newValue*mix : oldValue;
            }
            if (filterRamp_ && --filterRamp_ == 0) {
                activeFilterBank_ = nextBank;
                if (filterUpdatePending_) {
                    configureFilters(1 - activeFilterBank_);
                    filterRamp_ = static_cast<std::uint32_t>(sampleRate_ * 0.020);
                    filterUpdatePending_ = false;
                }
            }
            if (crossfeedEnabled_ && channels_ == 2) {
                const float l=x[0], r=x[1];
                crossfeedState_[0] += 0.12f * (r-crossfeedState_[0]);
                crossfeedState_[1] += 0.12f * (l-crossfeedState_[1]);
                x[0] = l*(1-crossfeedAmount_)+crossfeedState_[0]*crossfeedAmount_;
                x[1] = r*(1-crossfeedAmount_)+crossfeedState_[1]*crossfeedAmount_;
            }
            if (gainRamp_) { gain_ += gainStep_; --gainRamp_; if (!gainRamp_) gain_=targetGain_; }
            for (std::size_t ch=0; ch<channels_; ++ch) x[ch] *= gain_;
            if (limiterEnabled_) {
                float framePeak = 0.0f;
                for (std::size_t ch=0; ch<channels_; ++ch) framePeak = std::max(framePeak, std::abs(x[ch]));
                const bool transparent = !filterRamp_ && !gainRamp_ && gain_==1 && !crossfeedEnabled_ &&
                    std::none_of(filterEnabled_[activeFilterBank_].begin(),filterEnabled_[activeFilterBank_].end(),[](bool active){return active;});
                const float threshold=transparent ? 1.0f : limiterCeiling_;
                const float desired = framePeak > threshold ? limiterCeiling_ / framePeak : 1.0f;
                // Immediate attack prevents sample peaks exceeding the ceiling;
                // exponential release avoids hard-clipping distortion and clicks.
                if (desired < limiterGain_) limiterGain_ = desired;
                else limiterGain_ = std::min(desired,
                    1.0f - (1.0f - limiterGain_) * limiterReleaseCoeff_);
                for (std::size_t ch=0; ch<channels_; ++ch) x[ch] *= limiterGain_;
            } else {
                limiterGain_ = 1.0f;
            }
        }
        if (bypassRamp_) {
            bypassMix_ += bypassStep_;
            if (--bypassRamp_ == 0) bypassMix_ = bypass_ ? 1.0f : 0.0f;
        }
        for (std::size_t ch=0; ch<channels_; ++ch) {
            const float output = bypassMix_==1 ? original[ch] : bypassMix_==0 ? x[ch] : x[ch]*(1.0f-bypassMix_) + original[ch]*bypassMix_;
            data[n*channels_+ch]=output; outPeak=std::max(outPeak,std::abs(output)); outSq+=static_cast<double>(output)*output;
        }
    }
    for (std::size_t bank=0;bank<2;++bank)
        for(std::size_t ch=0;ch<channels_;++ch)
            for(std::size_t i=0;i<kMaxBands;++i)
                if(filterEnabled_[bank][i]) filters_[bank][ch][i].flushDenormals();
    inputMeter_={inPeak,static_cast<float>(std::sqrt(inSq/static_cast<double>(samples))),clipped};
    outputMeter_={outPeak,static_cast<float>(std::sqrt(outSq/static_cast<double>(samples))),outPeak>=1.0f};
}

std::string serializePreset(const Preset& p) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::setprecision(std::numeric_limits<double>::max_digits10)
      << "SONICSYNC_PRESET_V1\n" << p.name << '\n' << p.preampDb << '\n' << p.outputDb << '\n';
    for (const auto& b:p.bands)
        s << static_cast<unsigned>(b.type) << ' ' << b.frequency << ' ' << b.gainDb << ' '
          << b.q << ' ' << (b.enabled ? 1 : 0) << '\n';
    return s.str();
}
bool deserializePreset(std::string_view text, Preset& out) noexcept {
    try {
        const auto nl=text.find('\n'); if(nl==text.npos || text.substr(0,nl)!="SONICSYNC_PRESET_V1") return false;
        text.remove_prefix(nl+1); Preset p;
        auto readLine=[&](std::string& v)->bool { auto e=text.find('\n'); if(e==text.npos)return false; v.assign(text.substr(0,e)); text.remove_prefix(e+1); return true; };
        std::string line; if(!readLine(p.name)||p.name.empty()||p.name.size()>80||p.name.find_first_of("\r\n")!=p.name.npos||!readLine(line)||!parseDoubleLine(line,p.preampDb)) return false;
        if(!readLine(line)||!parseDoubleLine(line,p.outputDb))return false;
        for(auto& b:p.bands) {
            if(!readLine(line))return false; unsigned t=99; int en=0; std::string extra;
            std::istringstream fields(line);
            fields.imbue(std::locale::classic());
            if(!(fields>>t>>b.frequency>>b.gainDb>>b.q>>en)||(fields>>extra)||t>4||(en!=0&&en!=1))return false;
            b.type=static_cast<FilterType>(t); b.enabled=en!=0; if(!finiteBand(b,384000))return false;
        }
        if(!std::isfinite(p.preampDb)||!std::isfinite(p.outputDb)||p.preampDb < -36||p.preampDb>24||p.outputDb < -36||p.outputDb>12)return false;
        out=std::move(p); return true;
    } catch (...) { return false; }
}

double responseDb(const Preset& p, double frequency, double sr) noexcept {
    if (!std::isfinite(frequency)||!std::isfinite(sr)||sr<8000||frequency<=0||frequency>=sr*0.5) return 0;
    const double w=2*kPi*frequency/sr; double total=dbToGain(p.preampDb+p.outputDb);
    for(const auto& b:p.bands) if(b.enabled && b.frequency<sr*0.45) {
        const auto c=design(b,sr); const double cr=std::cos(w), ci=-std::sin(w), c2r=std::cos(2*w), c2i=-std::sin(2*w);
        const double nr=c.b0+c.b1*cr+c.b2*c2r, ni=c.b1*ci+c.b2*c2i;
        const double dr=c.a0+c.a1*cr+c.a2*c2r, di=c.a1*ci+c.a2*c2i;
        const double den=dr*dr+di*di; if(den>1e-30) total*=std::sqrt((nr*nr+ni*ni)/den);
    }
    return 20*std::log10(std::max(total,1e-12));
}

double peakResponseDb(const Preset& p, double sr) noexcept {
    if(std::none_of(p.bands.begin(),p.bands.end(),[](const Band& b){return b.enabled && b.gainDb>0;})) return 0;
    // Control-thread calculation. Sample DC, all band centers, and a dense log grid.
    double peak=0;
    for(int i=0;i<=512;++i) {
        const double hz=std::pow(sr*0.499, static_cast<double>(i)/512);
        peak=std::max(peak,responseDb(p,hz,sr));
    }
    for(const auto& b:p.bands) if(b.enabled) peak=std::max(peak,responseDb(p,b.frequency,sr));
    return peak+0.01;
}

Preset builtInPreset(std::string_view id) {
    EqState state;
    const auto* entry=std::find_if(std::begin(catalog), std::end(catalog), [id](const auto& e){return e.id==id;});
    if(entry!=std::end(catalog)) state.select(static_cast<int>(entry-std::begin(catalog)));
    return makeEqPreset(state,48000);
}
} // namespace sonicsync

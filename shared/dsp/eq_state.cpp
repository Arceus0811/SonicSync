#include "eq_state.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace sonicsync {
namespace {
constexpr int groupOf(int band) { return band < 3 ? 0 : band < 7 ? 1 : 2; }
}

void EqState::select(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(std::size(catalog))) return;
    profile = index;
    trim = 0;
    tone.fill(0);
    for (int i=0; i<10; ++i) bands[i] = static_cast<int>(std::lround(catalog[index].gains[i]*1000));
}
int EqState::effective(int i) const noexcept {
    return std::clamp(bands[i]+tone[groupOf(i)], -12000, 12000);
}
int EqState::toneValue(int group) const noexcept {
    int sum=0, count=0;
    for (int i=0; i<10; ++i) if (groupOf(i)==group) { sum+=effective(i); ++count; }
    return count ? sum/count : 0;
}
void EqState::setBand(int i, int gain) noexcept {
    if (i<0 || i>=10) return;
    const int group=groupOf(i);
    for (int j=0; j<10; ++j) if (groupOf(j)==group) bands[j]=effective(j);
    tone[group]=0;
    bands[i]=std::clamp(gain,-12000,12000);
    profile=custom;
}
void EqState::setTone(int group, int target) noexcept {
    if (group<0 || group>2) return;
    // Solve for a common offset even when a band reaches its travel limit.
    int lo=-24000, hi=24000;
    target=std::clamp(target,-12000,12000);
    while (lo<hi) {
        const int mid=lo+(hi-lo)/2;
        tone[group]=mid;
        if (toneValue(group)<target) lo=mid+1; else hi=mid;
    }
    tone[group]=lo;
    profile=custom;
}
void setGraphicBand(Band& band, int i, double gain, double sr) noexcept {
    const bool shelf=i==0 || i==9;
    band={i==0?FilterType::LowShelf:i==9?FilterType::HighShelf:FilterType::Peaking,
          centers[i],gain,shelf?0.7071067811865476:1.4142135623730951,
          gain!=0 && centers[i]<0.45*sr};
}
Preset makeEqPreset(const EqState& state, double sr) {
    Preset p;
    p.name=state.profile>=0 && state.profile<static_cast<int>(std::size(catalog))
        ? catalog[state.profile].name : std::string_view("Custom");
    for(int i=0;i<10;++i) setGraphicBand(p.bands[i],i,state.effective(i)/1000.0,sr);
    p.preampDb=state.trim/1000.0-std::max(0.0,peakResponseDb(p,sr));
    return p;
}
std::string serializeEq(const EqState& s) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out<<"EQ2 "<<(s.profile==EqState::custom ? std::string_view("custom") : catalog[s.profile].id)
       <<' '<<s.bypass<<' '<<s.trim;
    for(int v:s.bands) out<<' '<<v;
    for(int v:s.tone) out<<' '<<v;
    return out.str();
}
bool deserializeEq(std::string_view text, EqState& state) {
    std::istringstream in{std::string(text)}; in.imbue(std::locale::classic());
    EqState s; std::string tag,id;
    if(!(in>>tag>>id>>s.bypass>>s.trim) || tag!="EQ2" || s.trim < -12000 || s.trim>12000) return false;
    s.profile=EqState::custom;
    if(id!="custom") {
        const auto* e=std::find_if(std::begin(catalog),std::end(catalog),[&](const auto& entry){return entry.id==id;});
        if(e==std::end(catalog)) return false;
        s.profile=static_cast<int>(e-std::begin(catalog));
    }
    for(auto& v:s.bands) if(!(in>>v) || v < -12000 || v>12000) return false;
    for(auto& v:s.tone) if(!(in>>v) || v < -24000 || v>24000) return false;
    std::string extra;
    if(in>>extra) return false;
    if(s.profile>=0) {
        EqState preset; preset.select(s.profile); preset.bypass=s.bypass;
        if(preset!=s) s.profile=EqState::custom;
    }
    state=s; return true;
}
}

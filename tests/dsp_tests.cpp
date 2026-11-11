#include "dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace sonicsync;
namespace {
constexpr double kPi=3.14159265358979323846;
constexpr double kSampleRate=48000.0;
void require(bool ok,const char* message){if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
double rms(const float* data,std::size_t count){double sum=0;for(std::size_t i=0;i<count;++i)sum+=static_cast<double>(data[i])*data[i];return std::sqrt(sum/static_cast<double>(count));}
std::vector<float> sine(double hz,double seconds,float amplitude=0.05f){
    const auto n=static_cast<std::size_t>(kSampleRate*seconds);std::vector<float> x(n);
    for(std::size_t i=0;i<n;++i)x[i]=amplitude*static_cast<float>(std::sin(2*kPi*hz*static_cast<double>(i)/kSampleRate));return x;
}
double measuredGainDb(const Preset& preset,double hz){
    Processor p;require(p.prepare(kSampleRate,1),"prepare sine-response processor");p.setLimiter(false);p.setPresetRealtime(preset);
    auto x=sine(hz,1.25);const double in=rms(x.data()+static_cast<std::size_t>(kSampleRate*0.85),static_cast<std::size_t>(kSampleRate*0.35));
    p.process(x.data(),x.size());const double out=rms(x.data()+static_cast<std::size_t>(kSampleRate*0.85),static_cast<std::size_t>(kSampleRate*0.35));
    return 20*std::log10(out/in);
}
void stable(const std::vector<float>& data,const char* message){for(float x:data)require(std::isfinite(x),message);}
}

int main(){
    Processor p;
    for(double sr:{44100.0,48000.0,96000.0,192000.0})require(p.prepare(sr,2),"supported sample rate prepares");
    require(!p.prepare(1000,2),"rejects invalid sample rate");
    require(p.prepare(48000,2),"reprepare stereo");
    for(double sr:{8000.0,44100.0,48000.0,96000.0,192000.0,384000.0})for(std::uint8_t t=0;t<5;++t){
        Biquad filter;filter.configure(Band{static_cast<FilterType>(t),std::min(2000.0,sr*0.45),12.0,20.0,true},sr);filter.reset();
        for(int n=0;n<20000;++n){const float y=filter.process(n==0?1.0f:0.0f);require(std::isfinite(y)&&std::abs(y)<1000,"all filters remain stable at supported sample rates");}
    }

    Preset neutral=builtInPreset("neutral"), hiphop=builtInPreset("hip-hop");
    const double db60=measuredGainDb(hiphop,60),db100=measuredGainDb(hiphop,100),db1k=measuredGainDb(neutral,1000),db8k=measuredGainDb(neutral,8000);
    std::cout<<"Measured Hip-hop gain 60 Hz: "<<db60<<" dB; 100 Hz: "<<db100<<" dB; Neutral 1 kHz: "<<db1k<<" dB; Neutral 8 kHz: "<<db8k<<" dB\n";
    require(std::abs(db60-responseDb(hiphop,60,kSampleRate))<0.1,"Hip-hop EQ renders the catalog response at 60 Hz");
    require(std::abs(db100-responseDb(hiphop,100,kSampleRate))<0.1,"Hip-hop EQ renders the catalog response at 100 Hz");
    require(std::abs(db1k)<0.05&&std::abs(db8k)<0.05,"neutral preset preserves 1 kHz and 8 kHz within tolerance");

    Preset treble=neutral;treble.name="Treble test";for(auto& b:treble.bands)b.enabled=false;treble.bands[0]={FilterType::HighShelf,6000,6,0.707,true};
    const double treble8k=measuredGainDb(treble,8000);std::cout<<"Measured treble shelf gain 8 kHz: "<<treble8k<<" dB\n";
    require(treble8k>4.0&&treble8k<7.0,"high-shelf adjustment renders the catalog response at 8 kHz");

    Processor switcher;require(switcher.prepare(kSampleRate,1),"prepare live profile-switch test");switcher.setLimiter(false);switcher.setPresetRealtime(neutral);
    auto first=sine(250,0.5);switcher.process(first.data(),first.size());auto before=sine(250,0.35);switcher.process(before.data(),before.size());const double neutralRms=rms(before.data()+2400,before.size()-2400);
    switcher.setPresetRealtime(hiphop);auto after=sine(250,0.35);switcher.process(after.data(),after.size());const double hiphopRms=rms(after.data()+2400,after.size()-2400);
    const double switchedDb=20*std::log10(hiphopRms/neutralRms);std::cout<<"Live neutral-to-Hip-hop 250 Hz change: "<<switchedDb<<" dB\n";
    require(switchedDb < -3.0,"live preset switch changes processed output without re-preparing");
    float largestStep=0;for(std::size_t i=1;i<after.size();++i)largestStep=std::max(largestStep,std::abs(after[i]-after[i-1]));
    require(largestStep<0.01f,"preset transition has no large sample discontinuity");

    Processor dragging;
    require(dragging.prepare(kSampleRate, 1), "prepare rapid EQ updates");
    dragging.setLimiter(false);
    auto shelf = neutral;
    shelf.bands[0].enabled = true;
    shelf.bands[0].gainDb = 12;
    dragging.setPresetRealtime(shelf);
    std::vector<float> dc(1800, 0.1f);
    dragging.process(dc.data(), dc.size());
    const float beforeDrag = dc.back();
    shelf.bands[0].gainDb = 6;
    dragging.setPresetRealtime(shelf);
    shelf.bands[0].gainDb = -6;
    dragging.setPresetRealtime(shelf);
    std::vector<float> settled(9600, 0.1f);
    dragging.process(settled.data(), settled.size());
    require(std::abs(settled.front() - beforeDrag) < 0.002f, "dragging a live EQ slider preserves the audible crossfade");
    require(std::abs(settled.back() - 0.1f * std::pow(10.0f, -6.0f / 20)) < 0.0001f, "rapid EQ changes settle on the last value");

    Processor bypass;require(bypass.prepare(kSampleRate,1),"prepare bypass test");bypass.setLimiter(false);bypass.setPresetRealtime(hiphop);
    auto warm=sine(100,0.2);bypass.process(warm.data(),warm.size());bypass.setBypass(true);auto ramp=sine(100,0.03);bypass.process(ramp.data(),ramp.size());
    auto original=sine(100,0.05);auto copy=original;bypass.process(copy.data(),copy.size());
    require(copy==original,"settled bypass returns the original samples exactly");
    bypass.setBypass(false);auto restore=sine(100,0.04);bypass.process(restore.data(),restore.size());stable(restore,"bypass recovery stays finite");

    Preset gainPreset=neutral;gainPreset.preampDb=-6;require(std::abs(measuredGainDb(gainPreset,1000)+6)<0.08,"preamp gain staging matches -6 dB");
    Processor limiter;require(limiter.prepare(kSampleRate,2),"prepare limiter");std::vector<float> hot(1024,4.0f);limiter.process(hot.data(),hot.size()/2);
    require(limiter.outputMeter().peak<=0.8914f&&limiter.inputMeter().clipped,"limiter protects the configured ceiling and reports input clipping");

    Processor impulseProcessor;require(impulseProcessor.prepare(kSampleRate,1),"prepare impulse response");impulseProcessor.setLimiter(false);impulseProcessor.setPresetRealtime(hiphop);
    std::vector<float> impulse(8192);impulse[0]=1;impulseProcessor.process(impulse.data(),impulse.size());stable(impulse,"EQ impulse response remains finite");

    Processor sweepProcessor;require(sweepProcessor.prepare(kSampleRate,1),"prepare sweep processor");sweepProcessor.setPresetRealtime(hiphop);
    std::vector<float> sweep(static_cast<std::size_t>(kSampleRate*4));double phase=0;
    for(std::size_t i=0;i<sweep.size();++i){const double t=static_cast<double>(i)/static_cast<double>(sweep.size());const double hz=20.0*std::pow(1000.0,t);phase+=2*kPi*hz/kSampleRate;sweep[i]=0.05f*static_cast<float>(std::sin(phase));}
    sweepProcessor.process(sweep.data(),sweep.size());stable(sweep,"log sweep remains finite through EQ");

    Processor music;require(music.prepare(kSampleRate,2),"prepare music-like signal test");music.setPresetRealtime(hiphop);
    std::vector<float> mix(static_cast<std::size_t>(kSampleRate*10)*2);std::uint32_t state=0x12345678;
    for(std::size_t i=0;i<mix.size()/2;++i){state=1664525U*state+1013904223U;const float noise=(static_cast<float>(state>>8)/16777216.0f-0.5f)*0.02f;const float s=0.07f*static_cast<float>(std::sin(2*kPi*100*i/kSampleRate))+0.04f*static_cast<float>(std::sin(2*kPi*1000*i/kSampleRate))+noise;mix[2*i]=mix[2*i+1]=s;}
    music.process(mix.data(),mix.size()/2);stable(mix,"ten seconds of deterministic music-like content remain finite");
    const double programRms=rms(mix.data(),mix.size());require(programRms>0.01&&programRms<0.2,"music-like signal maintains safe RMS gain");

    Preset peak;peak.bands[0]={FilterType::Peaking,1000,6,1,true};
    require(responseDb(peak,1000,48000)>5.9&&responseDb(peak,100,48000)<0.1,"analytic response matches peaking-filter gain");
    Preset shelves;shelves.bands[0]={FilterType::LowShelf,100,6,0.707,true};shelves.bands[1]={FilterType::HighShelf,6000,-6,0.707,true};
    require(responseDb(shelves,30,48000)>5.8&&responseDb(shelves,18000,48000)<-5.8,"shelf response matches expected gain");
    Preset cut;cut.bands[0]={FilterType::HighPass,1000,0,0.707,true};cut.bands[1]={FilterType::LowPass,1000,0,0.707,true};
    require(responseDb(cut,100,48000)<-10&&responseDb(cut,10000,48000)<-10,"high-pass and low-pass attenuate out-of-band frequencies");
    const auto encoded=serializePreset(hiphop);Preset restored;
    require(deserializePreset(encoded,restored)&&restored.name==hiphop.name&&std::abs(restored.bands[0].gainDb-hiphop.bands[0].gainDb)<1e-5,"preset values round trip");
    require(!deserializePreset("SONICSYNC_PRESET_V1\nname\n0junk\n0\n",restored),"preset parser rejects numeric prefixes with trailing junk");
    std::cout<<"All DSP frequency, transition, bypass, impulse, sweep, limiter, and serialization checks passed\n";
}

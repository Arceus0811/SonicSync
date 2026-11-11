#include "eq_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include "../platforms/windows/eq_control.hpp"
#include <atomic>
#include <thread>
#endif
using namespace sonicsync;
void check(bool ok,const char* why){if(!ok) throw std::runtime_error(why);}
int main(){
    constexpr double pi=3.14159265358979323846;
    double worst=0, closest=100, loudnessRange=0;
    for(int rate:{44100,48000,96000,192000}){
        std::array<std::array<double,1024>,10> curves{};
        double loudMin=0,loudMax=0;
        for(int id=0;id<10;++id){
            EqState state; state.select(id);
            const auto preset=makeEqPreset(state,rate);
            for(int band=0;band<10;++band){
                Processor engine;check(engine.prepare(rate,1),"prepare");
                engine.setPresetRealtime(preset);
                const size_t tail=static_cast<size_t>(std::llround(rate*.64));
                std::vector<float> samples(tail+rate/2);
                for(size_t i=0;i<samples.size();++i) samples[i]=static_cast<float>(.1*std::sin(2*pi*centers[band]*i/rate));
                engine.process(samples.data(),samples.size());
                double square=0;
                for(size_t i=samples.size()-tail;i<samples.size();++i) square+=samples[i]*samples[i];
                const double measured=10*std::log10(square/tail/.005);
                worst=std::max(worst,std::abs(measured-catalog[id].response[band]));
                check(std::abs(measured-catalog[id].response[band])<.25,"catalog response");
            }
            double power=0;
            for(size_t i=0;i<curves[id].size();++i){
                double hz=20*std::pow(1000.0,static_cast<double>(i)/(curves[id].size()-1));
                double db=responseDb(preset,hz,rate);
                curves[id][i]=db;power+=std::pow(10.,db/10);
                if(id==0) check(std::abs(db)<.01,"neutral");
            }
            double pink=10*std::log10(power/curves[id].size());
            loudMin=std::min(loudMin,pink); loudMax=std::max(loudMax,pink);
            for(int other=0;other<id;++other){
                double diff=0;
                for(size_t i=0;i<curves[id].size();++i) diff=std::max(diff,std::abs(curves[id][i]-curves[other][i]));
                closest=std::min(closest,diff);
            }
        }
        loudnessRange=std::max(loudnessRange,loudMax-loudMin);
    }
    check(closest>=1.5,"indistinct presets");check(loudnessRange<=1,"pink spectrum power range");
    EqState s;s.select(1);auto music=serializeEq(s);s.select(6);s.select(1);check(serializeEq(s)==music,"round trip");
    s.setBand(3,12000);s.setTone(1,-2500);EqState saved;
    check(deserializeEq(serializeEq(s),saved)&&s==saved && s.profile==-1,"custom round trip");
    std::mt19937 random(37);
    for(int rate:{8000,16000,192000}){
        Processor engine;check(engine.prepare(rate,16),"multichannel");
        std::array<float,16*128> data{};
        for(int i=0;i<200;++i){
            s.select(static_cast<int>(random()%10));engine.setPresetRealtime(makeEqPreset(s,rate));
            data.fill(.05f);engine.process(data.data(),128);
            for(auto x:data)check(std::isfinite(x)&&std::abs(x)<1,"rapid changes");
        }
        engine.setBypass(true);
        for(int i=0;i<30;++i) engine.process(data.data(),128);
        data[0]=-0.f;data[1]=.9876543f;
        auto copy=data;engine.process(data.data(),128);check(std::memcmp(data.data(),copy.data(),sizeof(data))==0,"dry bytes");
    }
#ifdef _WIN32
    win::ControlBlock block{};std::atomic<bool> done=false;
    std::thread writer([&]{
        for(LONG i=1;i<=100000;++i){win::EqCommand c{i,0,-i};c.gains.fill(i);win::publishEq(block,c);}
        done=true;
    });
    LONG gen=0;win::EqCommand c;
    do{
        if(win::readEq(block,gen,c)){
            check(c.profile==-c.preamp,"torn gain");
            for(auto v:c.gains)check(v==c.profile,"torn bands");
        }
    }while(!done || gen!=win::load(&block.generation));
    writer.join();
#endif
    std::cout<<"400 measured tones: worst error "<<worst<<" dB; closest pair "<<closest
             <<" dB; pink-spectrum power range "<<loudnessRange<<" dB\n";
}

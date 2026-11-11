#include "eq_state.hpp"
#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {
using namespace sonicsync;
struct Stream {
    Processor processor;
    Preset preset;
    int channels,rate;
    std::array<float,256*Processor::kMaxChannels> scratch{};
};
EqState decode(JNIEnv* env,jstring text) {
    EqState state;
    if(text){
        const char* bytes=env->GetStringUTFChars(text,nullptr);
        if(bytes){deserializeEq(bytes,state);env->ReleaseStringUTFChars(text,bytes);}
    }
    return state;
}
}
#define JNI_EQ(name) Java_com_sonicsync_android_NativeEq_##name
extern "C" JNIEXPORT jstring JNICALL JNI_EQ(edit)(JNIEnv* e,jobject,jstring stored,jint operation,jint index,jint value){
    auto state=decode(e,stored);
    switch(operation){
        case 1: state.select(index); break;
        case 2: state.setBand(index,value); break;
        case 3: state.bypass=!value; break;
        default: break;
    }
    return e->NewStringUTF(serializeEq(state).c_str());
}
extern "C" JNIEXPORT jintArray JNICALL JNI_EQ(values)(JNIEnv* e,jobject,jstring stored,jint rate){
    auto s=decode(e,stored);
    auto p=makeEqPreset(s,rate);
    jint v[13]{s.profile,s.bypass,static_cast<int>(std::lround(p.preampDb*1000))};
    for(int i=0;i<10;++i) v[3+i]=s.effective(i);
    auto result=e->NewIntArray(13);
    if(result)e->SetIntArrayRegion(result,0,13,v);
    return result;
}
extern "C" JNIEXPORT jlong JNICALL JNI_EQ(create)(JNIEnv*,jobject,jint rate,jint channels){
    if(channels<1 || channels>static_cast<jint>(Processor::kMaxChannels)) return 0;
    auto* stream=new(std::nothrow) Stream{};
    if(!stream)return 0;
    if(!stream->processor.prepare(rate,channels)){delete stream;return 0;}
    stream->channels=channels;stream->rate=rate;return reinterpret_cast<jlong>(stream);
}
extern "C" JNIEXPORT void JNICALL JNI_EQ(release)(JNIEnv*,jobject,jlong handle){delete reinterpret_cast<Stream*>(handle);}
extern "C" JNIEXPORT void JNICALL JNI_EQ(apply)(JNIEnv* e,jobject,jlong handle,jintArray values){
    auto* s=reinterpret_cast<Stream*>(handle);
    if(!s || !values || e->GetArrayLength(values)<13) return;
    jint v[13];e->GetIntArrayRegion(values,0,13,v);
    for(int i=0;i<10;++i)setGraphicBand(s->preset.bands[i],i,v[i+3]/1000.,s->rate);
    s->preset.preampDb=v[2]/1000.;s->processor.setPresetRealtime(s->preset);s->processor.setBypass(v[1]!=0);
}
extern "C" JNIEXPORT void JNICALL JNI_EQ(process)(JNIEnv* e,jobject,jlong handle,jobject buffer,jint bytes){
    auto* s=reinterpret_cast<Stream*>(handle);
    if(!s || !buffer) return;
    auto* pcm=static_cast<int16_t*>(e->GetDirectBufferAddress(buffer));
    if(!pcm || bytes<0 || bytes>e->GetDirectBufferCapacity(buffer))return;
    int frames=bytes/(2*s->channels);
    while(frames>0){
        const int n=std::min(256,frames), count=n*s->channels;
        for(int i=0;i<count;++i)s->scratch[i]=pcm[i]/32768.f;
        const bool dry=s->processor.dry();
        s->processor.process(s->scratch.data(),n);
        if(!dry)for(int i=0;i<count;++i)pcm[i]=static_cast<int16_t>(std::clamp(std::lrintf(s->scratch[i]*32768.f),-32768L,32767L));
        pcm+=count;frames-=n;
    }
}

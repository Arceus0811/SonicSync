#define NOMINMAX
#include "dsp.hpp"
#include "audio_analysis.hpp"
#include "wave_reader.hpp"
#include "eq_control.hpp"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace sonicsync;

namespace {
constexpr REFERENCE_TIME kBufferDuration = 200000;
struct Endpoint { std::wstring id; std::wstring name; bool isDefault{false}; };
using sonicsync::win::WaveFile;

float decode(const BYTE* p, WORD bits, bool isFloat) {
    if (isFloat) { float v=0; std::memcpy(&v,p,sizeof(v)); return std::isfinite(v)?std::clamp(v,-16.0f,16.0f):0.0f; }
    switch(bits) {
    case 8: return (static_cast<float>(*p)-128.0f)/128.0f;
    case 16: { std::int16_t v; std::memcpy(&v,p,2); return static_cast<float>(v)/32768.0f; }
    case 24: { std::int32_t v=static_cast<std::int32_t>(p[0])|(static_cast<std::int32_t>(p[1])<<8)|(static_cast<std::int32_t>(p[2])<<16); if(v&0x00800000)v|=static_cast<std::int32_t>(0xff000000); return static_cast<float>(v)/8388608.0f; }
    case 32: { std::int32_t v; std::memcpy(&v,p,4); return static_cast<float>(static_cast<double>(v)/2147483648.0); }
    default: return 0.0f;
    }
}

HRESULT endpoints(std::vector<Endpoint>& out) {
    ComPtr<IMMDeviceEnumerator> e;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)); if(FAILED(hr))return hr;
    ComPtr<IMMDevice> def; std::wstring defId;
    if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&def))) { LPWSTR id=nullptr; if(SUCCEEDED(def->GetId(&id))){defId=id;CoTaskMemFree(id);} }
    ComPtr<IMMDeviceCollection> c; if(FAILED(hr=e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&c)))return hr;
    UINT count=0; if(FAILED(hr=c->GetCount(&count)))return hr;
    for(UINT i=0;i<count;++i){ ComPtr<IMMDevice>d; if(FAILED(hr=c->Item(i,&d)))return hr; LPWSTR id=nullptr; if(FAILED(hr=d->GetId(&id)))return hr;
        Endpoint x; x.id=id; CoTaskMemFree(id); x.isDefault=x.id==defId; ComPtr<IPropertyStore> props;
        if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&props))){PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)x.name=v.pwszVal;PropVariantClear(&v);}
        if(x.name.empty())x.name=L"Unnamed render endpoint";out.push_back(std::move(x)); }
    return S_OK;
}

bool indexOf(const wchar_t* s,std::size_t& n){if(!s||!s[0])return false;wchar_t* end=nullptr;const auto v=std::wcstoul(s,&end,10);if(!end||*end)return false;n=static_cast<std::size_t>(v);return true;}
bool toPresetId(const std::wstring& wide, std::string& id) {
    static const std::vector<std::string> ids{"neutral","music","hip-hop","rnb","classical","acoustic","rock","movies","gaming","voice"};
    id.clear(); for(wchar_t c:wide){if(c<0||c>0x7f)return false;id.push_back(static_cast<char>(c));}
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}

HRESULT play(const Endpoint& endpoint, const WaveFile& wave, bool bypass, const std::string& presetName, HANDLE stopEvent, HANDLE playEvent, sonicsync::win::ControlBlock* controls) {
    ComPtr<IMMDeviceEnumerator> e; HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)); if(FAILED(hr))return hr;
    ComPtr<IMMDevice>d; if(FAILED(hr=e->GetDevice(endpoint.id.c_str(),&d)))return hr;
    ComPtr<IAudioClient> client; if(FAILED(hr=d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client)))return hr;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!event)return HRESULT_FROM_WIN32(GetLastError());
    auto close=[&]{CloseHandle(event);};
    auto fmt=const_cast<WAVEFORMATEX*>(&wave.format.Format);
    const DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,kBufferDuration,0,fmt,nullptr);
    if(FAILED(hr)){close();return hr;}
    if(FAILED(hr=client->SetEventHandle(event))){close();return hr;}
    UINT32 bufferFrames=0; if(FAILED(hr=client->GetBufferSize(&bufferFrames))){close();return hr;}
    ComPtr<IAudioRenderClient> render; if(FAILED(hr=client->GetService(IID_PPV_ARGS(&render)))){close();return hr;}
    const auto& f=wave.format.Format; const bool isFloat=f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT || (f.wFormatTag==WAVE_FORMAT_EXTENSIBLE&&IsEqualGUID(wave.format.SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
    const std::size_t bytesPerSample=f.wBitsPerSample/8, totalFrames=wave.audio.size()/f.nBlockAlign;
    Processor processor; if(!processor.prepare(f.nSamplesPerSec,f.nChannels)){close();return E_INVALIDARG;}
    if(!presetName.empty()) processor.setPreset(builtInPreset(presetName));
    processor.setBypass(bypass);
    sonicsync::win::LiveEq liveEq;
    if(controls) sonicsync::win::store(&controls->sampleRate,static_cast<LONG>(f.nSamplesPerSec));
    liveEq.update(controls,processor,f.nSamplesPerSec);
    processor.prepare(f.nSamplesPerSec,f.nChannels);
    std::array<float,16> spectrumCoefficients{};
    for(std::size_t i=0;i<spectrumCoefficients.size();++i){const double t=static_cast<double>(i)/static_cast<double>(spectrumCoefficients.size()-1);const double hz=30.0*std::pow(18000.0/30.0,t);spectrumCoefficients[i]=static_cast<float>(2.0*std::cos(2.0*3.141592653589793*hz/f.nSamplesPerSec));}
    std::vector<float> block(static_cast<std::size_t>(bufferFrames)*f.nChannels);
    DWORD taskIndex=0; HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&taskIndex);
    if(FAILED(hr=client->Start())){if(mmcss)AvRevertMmThreadCharacteristics(mmcss);close();return hr;}
    std::size_t cursor=0; float inputPeak=0.0f, processedPeak=0.0f; bool stopped=false; unsigned controlUpdates=0;
    while(cursor<totalFrames){
        HANDLE waits[2]{event,stopEvent};
        const DWORD wait=stopEvent?WaitForMultipleObjects(2,waits,FALSE,2000):WaitForSingleObject(event,2000);
        if(stopEvent&&wait==WAIT_OBJECT_0+1){stopped=true;break;}
        if(wait!=WAIT_OBJECT_0){hr=wait==WAIT_TIMEOUT?HRESULT_FROM_WIN32(WAIT_TIMEOUT):HRESULT_FROM_WIN32(GetLastError());break;}
        if (playEvent && WaitForSingleObject(playEvent, 0) == WAIT_TIMEOUT) {
            // IAudioClient::Stop preserves already submitted frames for Start.
            if (FAILED(hr = client->Stop())) break;
            HANDLE transport[]{stopEvent, playEvent};
            const DWORD resumed = WaitForMultipleObjects(2, transport, FALSE, INFINITE);
            if (resumed == WAIT_OBJECT_0) { stopped = true; break; }
            if (resumed != WAIT_OBJECT_0 + 1) { hr = HRESULT_FROM_WIN32(GetLastError()); break; }
            if (FAILED(hr = client->Start())) break;
            continue;
        }
        UINT32 padding=0; if(FAILED(hr=client->GetCurrentPadding(&padding)))break;
        const UINT32 writable=bufferFrames-padding; if(!writable)continue;
        const UINT32 frames=static_cast<UINT32>(std::min<std::size_t>(writable,totalFrames-cursor));
        BYTE* output=nullptr; if(FAILED(hr=render->GetBuffer(frames,&output)))break;
        // Apply control changes on the render thread. Preset coefficient banks
        // and gain are crossfaded by Processor::setPreset/process().
        if(liveEq.update(controls,processor,f.nSamplesPerSec)) ++controlUpdates;
        const bool copyDry = processor.dry();
        const std::size_t blockSamples=static_cast<std::size_t>(frames)*f.nChannels;
        for(std::size_t i=0;i<blockSamples;++i){block[i]=decode(wave.audio.data()+(cursor*f.nChannels+i)*bytesPerSample,f.wBitsPerSample,isFloat);inputPeak=std::max(inputPeak,std::abs(block[i]));}
        processor.process(block.data(),frames);
        if (controls && ((cursor / std::max<std::size_t>(1,bufferFrames*4U)) != ((cursor+frames) / std::max<std::size_t>(1,bufferFrames*4U))))
            sonicsync::win::publishAnalysis(block.data(),frames,f.nChannels,spectrumCoefficients,processor,controls);
        if(copyDry) {
            std::memcpy(output,wave.audio.data()+cursor*f.nBlockAlign,frames*f.nBlockAlign);
            processedPeak=std::max(processedPeak,processor.outputMeter().peak);
        } else for(std::size_t i=0;i<blockSamples;++i){
            processedPeak=std::max(processedPeak,std::abs(block[i]));
            const float x=std::clamp(block[i],-1.0f,1.0f); BYTE* out=output+i*bytesPerSample;
            if(isFloat)std::memcpy(out,&x,sizeof(x));
            else if(f.wBitsPerSample==8)out[0]=static_cast<BYTE>(std::clamp(static_cast<int>(std::lrint(x*127.0f+128.0f)),0,255));
            else if(f.wBitsPerSample==16){const auto v=static_cast<std::int16_t>(std::lrint(x*32767.0f));std::memcpy(out,&v,2);}
            else if(f.wBitsPerSample==24){const auto v=static_cast<std::int32_t>(std::lrint(x*8388607.0f));out[0]=static_cast<BYTE>(v);out[1]=static_cast<BYTE>(v>>8);out[2]=static_cast<BYTE>(v>>16);}
            else {const auto v=static_cast<std::int32_t>(std::llrint(static_cast<double>(x)*2147483647.0));std::memcpy(out,&v,4);}
        }
        if(FAILED(hr=render->ReleaseBuffer(frames,0)))break;
        cursor+=frames;
    }
    if(SUCCEEDED(hr) && !stopped){
        // Drain the final shared-mode buffer so the end of the file is heard.
        UINT32 padding=0;if(SUCCEEDED(client->GetCurrentPadding(&padding))){const auto ms=static_cast<DWORD>((static_cast<std::uint64_t>(padding)*1000)/f.nSamplesPerSec);Sleep(ms+5);}
    }
    std::wcout<<L"Submitted "<<cursor<<L" / "<<totalFrames<<L" frames; decoded peak "<<inputPeak<<L", post-DSP peak "<<processedPeak<<(stopped?L" (stopped)":L"")<<L".\n";
    if(controls) std::wcout<<L"Live control updates "<<controlUpdates<<L"; bypass "<<(liveEq.bypass?L"on":L"off")<<L".\n";
    client->Stop(); if(mmcss)AvRevertMmThreadCharacteristics(mmcss); close(); return hr;
}

void usage(){std::wcout<<L"SonicSync WAV player: sonicsync_player.exe <file.wav> <endpoint-index> [--bypass] [--preset <neutral|music|hip-hop|rnb|classical|acoustic|rock|movies|gaming|voice>]\n                 or sonicsync_player.exe <file.wav> --device-id <endpoint-id> [options]\nRun sonicsync_wasapi.exe --list to see output endpoints. Audio is played only by this app on the selected endpoint.\n";}
}

int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--help"){usage();return 0;}
    if(argc<3){usage();return 2;}
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED); if(FAILED(init)){std::wcerr<<L"COM init failed: 0x"<<std::hex<<init<<L"\n";return 1;}
    std::vector<Endpoint> list; HRESULT hr=endpoints(list); int code=0;
    int optionStart=3; std::size_t idx=0; const Endpoint* selected=nullptr;
    if(SUCCEEDED(hr)&&std::wstring(argv[2])==L"--device-id"&&argc>=4){const std::wstring target=argv[3];optionStart=4;for(const auto& ep:list)if(ep.id==target){selected=&ep;break;}}
    else if(SUCCEEDED(hr)&&indexOf(argv[2],idx)&&idx<list.size())selected=&list[idx];
    if(FAILED(hr)||!selected){std::wcerr<<L"Invalid endpoint index/device id or endpoint enumeration failed. Use sonicsync_wasapi.exe --list.\n";code=2;}
    else {
        WaveFile wave;std::wstring error;
        std::ifstream input(argv[1], std::ios::binary);
        const bool loaded = sonicsync::win::readWave(input, wave, error);
        input.close();
        if(!loaded){std::wcerr<<error<<L"\n";code=2;}
        else {
            bool bypass=false;std::string preset;std::wstring stopName, playName, controlName;bool valid=true;
            for(int i=optionStart;i<argc;++i){if(std::wstring(argv[i])==L"--bypass")bypass=true;else if(std::wstring(argv[i])==L"--preset"&&i+1<argc){if(!toPresetId(argv[++i],preset))valid=false;}else if(std::wstring(argv[i])==L"--stop-event"&&i+1<argc)stopName=argv[++i];else if(std::wstring(argv[i])==L"--play-event"&&i+1<argc)playName=argv[++i];else if(std::wstring(argv[i])==L"--control-map"&&i+1<argc)controlName=argv[++i];else valid=false;}
            if(!valid || (!playName.empty() && stopName.empty())){usage();code=2;}else{
                HANDLE stopEvent=stopName.empty()?nullptr:OpenEventW(SYNCHRONIZE,FALSE,stopName.c_str());
                if(!stopName.empty()&&!stopEvent){std::wcerr<<L"Could not open the player stop event.\n";code=3;}
                else {
                    HANDLE playEvent = playName.empty() ? nullptr : OpenEventW(SYNCHRONIZE,FALSE,playName.c_str());
                    if (!playName.empty() && !playEvent) {
                        std::wcerr << L"Could not open the playback pause control.\n";
                        CloseHandle(stopEvent);
                        CoUninitialize();
                        return 3;
                    }
                    HANDLE mapping=controlName.empty()?nullptr:OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,controlName.c_str());
                    auto* controls=mapping?static_cast<sonicsync::win::ControlBlock*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(sonicsync::win::ControlBlock))):nullptr;
                    if(!controlName.empty()&&(!controls || sonicsync::win::load(&controls->version)!=sonicsync::win::eqProtocol)){std::wcerr<<L"Could not open the playback control channel.\n";if(controls)UnmapViewOfFile(controls);if(mapping)CloseHandle(mapping);if(playEvent)CloseHandle(playEvent);if(stopEvent)CloseHandle(stopEvent);CoUninitialize();return 3;}
                    std::wcout<<L"Playing "<<argv[1]<<L" through "<<selected->name<<(bypass?L" (DSP bypass)":L" (EQ enabled)")<<L".\n";
                    hr=play(*selected,wave,bypass,preset,stopEvent,playEvent,controls);if(FAILED(hr)){std::wcerr<<L"Playback failed: 0x"<<std::hex<<hr<<L"\n";code=3;}
                    if(controls)UnmapViewOfFile(controls);if(mapping)CloseHandle(mapping);
                    if(playEvent)CloseHandle(playEvent);
                    if(stopEvent)CloseHandle(stopEvent);
                }
            }
        }
    }
    CoUninitialize();return code;
}

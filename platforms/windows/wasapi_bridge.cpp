#include "dsp.hpp"
#include "audio_analysis.hpp"
#include "eq_control.hpp"

#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace sonicsync;

namespace {
constexpr REFERENCE_TIME kBufferDuration = 200000; // 20 ms
std::atomic<bool> g_stop{false};
HANDLE g_stopEvent = nullptr;

BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_stop.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

struct Endpoint {
    std::wstring id;
    std::wstring name;
    bool isDefault{false};
};

HRESULT listEndpoints(std::vector<Endpoint>& endpoints) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> defaultDevice;
    std::wstring defaultId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id))) { defaultId = id; CoTaskMemFree(id); }
    }
    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return hr;
    UINT count = 0;
    if (FAILED(hr = collection->GetCount(&count))) return hr;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(hr = collection->Item(i, &device))) return hr;
        LPWSTR rawId = nullptr;
        if (FAILED(hr = device->GetId(&rawId))) return hr;
        Endpoint endpoint;
        endpoint.id = rawId;
        CoTaskMemFree(rawId);
        endpoint.isDefault = endpoint.id == defaultId;
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                endpoint.name = value.pwszVal;
            PropVariantClear(&value);
        }
        if (endpoint.name.empty()) endpoint.name = L"Unnamed render endpoint";
        endpoints.push_back(std::move(endpoint));
    }
    return S_OK;
}

bool parseIndex(const wchar_t* text, std::size_t& index) {
    if (!text || !*text) return false;
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(text, &end, 10);
    if (*end != L'\0') return false;
    index = static_cast<std::size_t>(value);
    return true;
}

bool identifyFormat(const WAVEFORMATEX* format, bool& isFloat) {
    GUID subFormat{};
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        subFormat = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat;
        if (IsEqualGUID(subFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) { isFloat = true; return format->wBitsPerSample == 32; }
        if (IsEqualGUID(subFormat, KSDATAFORMAT_SUBTYPE_PCM)) { isFloat = false; }
        else return false;
    } else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (format->wFormatTag == WAVE_FORMAT_PCM) {
        isFloat = false;
    } else {
        return false;
    }
    return isFloat ? format->wBitsPerSample == 32 :
        (format->wBitsPerSample == 8 || format->wBitsPerSample == 16 ||
         format->wBitsPerSample == 24 || format->wBitsPerSample == 32);
}

float decodeSample(const BYTE* data, WORD bits, bool isFloat) {
    if (isFloat) {
        float value;
        memcpy(&value, data, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -8.0f, 8.0f) : 0.0f;
    }
    switch (bits) {
    case 8: return (static_cast<float>(*data) - 128.0f) / 128.0f;
    case 16: {
        std::int16_t value; memcpy(&value, data, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
        std::int32_t value = static_cast<std::int32_t>(data[0]) |
            (static_cast<std::int32_t>(data[1]) << 8) | (static_cast<std::int32_t>(data[2]) << 16);
        if (value & 0x00800000) value |= static_cast<std::int32_t>(0xff000000);
        return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
        std::int32_t value; memcpy(&value, data, sizeof(value));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    default: return 0.0f;
    }
}

void encodeSample(float sample, BYTE* data, WORD bits, bool isFloat) {
    sample = std::clamp(std::isfinite(sample) ? sample : 0.0f, -1.0f, 1.0f);
    if (isFloat) { memcpy(data, &sample, sizeof(sample)); return; }
    switch (bits) {
    case 8: *data = static_cast<BYTE>(std::clamp(static_cast<int>(std::lrint(sample * 127.0f + 128.0f)), 0, 255)); break;
    case 16: {
        const auto value = static_cast<std::int16_t>(std::lrint(sample * 32767.0f));
        memcpy(data, &value, sizeof(value)); break;
    }
    case 24: {
        const auto value = static_cast<std::int32_t>(std::lrint(sample * 8388607.0f));
        data[0] = static_cast<BYTE>(value); data[1] = static_cast<BYTE>(value >> 8); data[2] = static_cast<BYTE>(value >> 16); break;
    }
    case 32: {
        const auto value = static_cast<std::int32_t>(std::llrint(static_cast<double>(sample) * 2147483647.0));
        memcpy(data, &value, sizeof(value)); break;
    }
    }
}

class SampleQueue {
public:
    explicit SampleQueue(std::size_t samples) : data_(std::max<std::size_t>(samples, 1)) {}
    std::size_t free() const { return data_.size() - size_; }
    bool push(const float* input, std::size_t count) {
        if (count > free()) return false;
        for (std::size_t i=0;i<count;++i) { data_[write_] = input[i]; write_=(write_+1)%data_.size(); }
        size_ += count; return true;
    }
    bool pop(float& value) {
        if (!size_) return false;
        value=data_[read_]; read_=(read_+1)%data_.size(); --size_; return true;
    }
private:
    std::vector<float> data_;
    std::size_t read_{0}, write_{0}, size_{0};
};

HRESULT openClient(IMMDevice* device, bool capture, const WAVEFORMATEX* format,
                   HANDLE event, ComPtr<IAudioClient>& client, UINT32& bufferFrames) {
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr)) return hr;
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (capture) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, format, nullptr);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = client->SetEventHandle(event))) return hr;
    return client->GetBufferSize(&bufferFrames);
}

HRESULT runBridge(const Endpoint& captureEndpoint, const Endpoint& renderEndpoint,
                  sonicsync::win::ControlBlock* controls) {
#if defined(SONICSYNC_VIRTUAL_ENDPOINT_RELAY)
    if (captureEndpoint.name.find(L"SonicSync") == std::wstring::npos) {
        std::wcerr << L"The source must be the SonicSync virtual render endpoint.\n";
        return E_INVALIDARG;
    }
    if (renderEndpoint.name.find(L"SonicSync") != std::wstring::npos) {
        std::wcerr << L"Choose a separate physical output endpoint.\n";
        return E_INVALIDARG;
    }
#endif
    if (captureEndpoint.id == renderEndpoint.id) {
        std::wcerr << L"Choose two different render endpoints. Loopback-to-same-endpoint creates duplicate audio.\n";
        return E_INVALIDARG;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> captureDevice, renderDevice;
    if (FAILED(hr = enumerator->GetDevice(captureEndpoint.id.c_str(), &captureDevice))) return hr;
    if (FAILED(hr = enumerator->GetDevice(renderEndpoint.id.c_str(), &renderDevice))) return hr;

    ComPtr<IAudioClient> captureClient, renderClient;
    WAVEFORMATEX* rawFormat = nullptr;
    if (FAILED(hr = captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &captureClient))) return hr;
    if (FAILED(hr = captureClient->GetMixFormat(&rawFormat))) return hr;
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(rawFormat, CoTaskMemFree);
    const WORD channels = format->nChannels;
    bool isFloat = false;
    if ((channels < 1 || channels > Processor::kMaxChannels) || format->nBlockAlign == 0 || !identifyFormat(format.get(), isFloat)) {
        std::wcerr << L"The bridge supports 1 to 16 channel PCM 8/16/24/32-bit or float32 endpoint mix formats.\n";
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    HANDLE captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE renderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent || !renderEvent) { hr=HRESULT_FROM_WIN32(GetLastError()); if(captureEvent)CloseHandle(captureEvent);if(renderEvent)CloseHandle(renderEvent);return hr; }
    auto closeEvents = [&] { CloseHandle(captureEvent); CloseHandle(renderEvent); };
    UINT32 captureFrames=0, renderFrames=0;
    hr = openClient(captureDevice.Get(), true, format.get(), captureEvent, captureClient, captureFrames);
    if (FAILED(hr)) { closeEvents(); return hr; }
    hr = openClient(renderDevice.Get(), false, format.get(), renderEvent, renderClient, renderFrames);
    if (FAILED(hr)) { closeEvents(); return hr; }
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    if (FAILED(hr = captureClient->GetService(IID_PPV_ARGS(&capture)))) { closeEvents(); return hr; }
    if (FAILED(hr = renderClient->GetService(IID_PPV_ARGS(&render)))) { closeEvents(); return hr; }

    const std::size_t samplesPerFrame = channels;
    const std::size_t captureCapacity = static_cast<std::size_t>(captureFrames)*samplesPerFrame;
    std::vector<float> block(captureCapacity);
    std::array<float,16> spectrumCoefficients{};
    for(std::size_t i=0;i<spectrumCoefficients.size();++i){const double t=static_cast<double>(i)/static_cast<double>(spectrumCoefficients.size()-1);const double hz=30.0*std::pow(18000.0/30.0,t);spectrumCoefficients[i]=static_cast<float>(2.0*std::cos(2.0*3.141592653589793*hz/format->nSamplesPerSec));}
    SampleQueue queue(std::max<std::size_t>(static_cast<std::size_t>(format->nSamplesPerSec)*channels,
                                           static_cast<std::size_t>(renderFrames)*channels*4));
    Processor processor;
    if (!processor.prepare(format->nSamplesPerSec, channels)) { closeEvents(); return E_INVALIDARG; }
    processor.setLimiter(true, -1.0f);
    sonicsync::win::LiveEq liveEq;
    if(controls) sonicsync::win::store(&controls->sampleRate,static_cast<LONG>(format->nSamplesPerSec));
    liveEq.update(controls,processor,format->nSamplesPerSec);
    processor.prepare(format->nSamplesPerSec,channels);
    const std::size_t bytesPerSample = format->wBitsPerSample/8;
    HANDLE events[3]{captureEvent, renderEvent, g_stopEvent};
    DWORD taskIndex=0;
    HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (FAILED(hr = renderClient->Start())) { if(mmcss)AvRevertMmThreadCharacteristics(mmcss);closeEvents();return hr; }
    if (FAILED(hr = captureClient->Start())) { renderClient->Stop();if(mmcss)AvRevertMmThreadCharacteristics(mmcss);closeEvents();return hr; }
    if(controls)sonicsync::win::store(&controls->routeStatus,1);
    std::wcout << L"Processing real endpoint loopback from: " << captureEndpoint.name << L"\nTo: " << renderEndpoint.name
               << L"\nPress Ctrl+C to stop. Ensure the source endpoint is not also audible, or raw and processed audio will both play.\n";
    std::uint64_t droppedFrames=0;unsigned analysisPackets=0;float capturedPeak=0.0f,processedPeak=0.0f;
    while (!g_stop.load(std::memory_order_relaxed)) {
        const DWORD wait = WaitForMultipleObjects(g_stopEvent?3:2, events, FALSE, 250);
        if (wait == WAIT_TIMEOUT) continue;
        if (wait == WAIT_FAILED) { hr=HRESULT_FROM_WIN32(GetLastError());break; }
        if (g_stopEvent&&wait==WAIT_OBJECT_0+2) { g_stop.store(true,std::memory_order_relaxed); break; }
        if (wait == WAIT_OBJECT_0) {
            UINT32 packetFrames=0;
            for (;;) {
                hr=capture->GetNextPacketSize(&packetFrames);
                if(FAILED(hr)||packetFrames==0)break;
                BYTE* input=nullptr; DWORD flags=0; UINT32 frames=0;
                hr=capture->GetBuffer(&input,&frames,&flags,nullptr,nullptr);
                if (FAILED(hr)) break;
                if (frames > captureFrames || static_cast<std::size_t>(frames)*channels > block.size()) {
                    capture->ReleaseBuffer(frames); hr=E_UNEXPECTED; break;
                }
                for(UINT32 f=0;f<frames;++f) for(WORD c=0;c<channels;++c) {
                    const float sample=(flags&AUDCLNT_BUFFERFLAGS_SILENT)?0.0f:decodeSample(input+static_cast<std::size_t>(f)*format->nBlockAlign+c*bytesPerSample,format->wBitsPerSample,isFloat);
                    block[static_cast<std::size_t>(f)*channels+c]=sample;capturedPeak=std::max(capturedPeak,std::abs(sample));
                }
                liveEq.update(controls,processor,format->nSamplesPerSec);
                processor.process(block.data(),frames);
                for(std::size_t i=0;i<static_cast<std::size_t>(frames)*channels;++i)processedPeak=std::max(processedPeak,std::abs(block[i]));
                if(controls)sonicsync::win::store(&controls->routeStatus,processor.inputMeter().peak>1.0e-5f?2:1);
                if(controls&&((++analysisPackets%4)==0))sonicsync::win::publishAnalysis(block.data(),frames,channels,spectrumCoefficients,processor,controls);
                if(!queue.push(block.data(),static_cast<std::size_t>(frames)*channels)) droppedFrames+=frames;
                const HRESULT releaseHr=capture->ReleaseBuffer(frames);
                if(FAILED(releaseHr)){hr=releaseHr;break;}
            }
            if(FAILED(hr)) break;
        } else if (wait == WAIT_OBJECT_0+1) {
            UINT32 padding=0;
            if(FAILED(hr=renderClient->GetCurrentPadding(&padding)))break;
            const UINT32 writable=renderFrames-padding;
            if(!writable)continue;
            BYTE* output=nullptr;
            if(FAILED(hr=render->GetBuffer(writable,&output)))break;
            for(UINT32 f=0;f<writable;++f) for(WORD c=0;c<channels;++c) {
                float sample=0;
                queue.pop(sample);
                encodeSample(sample,output+static_cast<std::size_t>(f)*format->nBlockAlign+c*bytesPerSample,format->wBitsPerSample,isFloat);
            }
            if(FAILED(hr=render->ReleaseBuffer(writable,0)))break;
        }
    }
    captureClient->Stop(); renderClient->Stop();
    if(controls)sonicsync::win::store(&controls->routeStatus,0);
    if(mmcss)AvRevertMmThreadCharacteristics(mmcss);
    closeEvents();
    std::wcout<<L"Route sample peaks: captured="<<capturedPeak<<L", processed="<<processedPeak<<L".\n";
    if(droppedFrames) std::wcerr << L"Warning: dropped " << droppedFrames << L" captured frames because the render queue filled.\n";
    return SUCCEEDED(hr) ? S_OK : hr;
}

void printUsage() {
#if defined(SONICSYNC_VIRTUAL_ENDPOINT_RELAY)
    std::wcout << L"SonicSync virtual-endpoint relay (user mode)\n  sonicsync_virtual_endpoint_relay.exe --list\n  sonicsync_virtual_endpoint_relay.exe --run <SonicSync-source-index> <physical-render-index> [--control-map name] [--stop-event name]\n"
                  L"  sonicsync_virtual_endpoint_relay.exe --run-id <SonicSync-source-endpoint-id> <physical-render-endpoint-id> [--control-map name] [--stop-event name]\n"
                  L"Route applications explicitly to SonicSync Virtual Output. This command does not change Windows routing.\n";
#else
    std::wcout << L"SonicSync WASAPI routed processor\n  sonicsync_wasapi.exe --list\n  sonicsync_wasapi.exe --run <source-render-index> <physical-render-index> [--control-map name] [--stop-event name]\n"
                  L"  sonicsync_wasapi.exe --run-id <source-endpoint-id> <physical-endpoint-id> [--control-map name] [--stop-event name]\n"
                  L"Route external applications to a separate virtual playback endpoint first (for example, a virtual cable); this tool does not change Windows routing.\n";
#endif
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init)){std::wcerr<<L"COM initialization failed: 0x"<<std::hex<<init<<L"\n";return 1;}
    int result=0;
    std::vector<Endpoint> endpoints;
    HRESULT hr=listEndpoints(endpoints);
    if(FAILED(hr)) {std::wcerr<<L"Endpoint enumeration failed: 0x"<<std::hex<<hr<<L"\n";result=2;}
    else if(argc==2 && std::wstring(argv[1])==L"--list") {
        for(std::size_t i=0;i<endpoints.size();++i) std::wcout<<i<<L": "<<endpoints[i].name<<(endpoints[i].isDefault?L" [default]":L"")<<L"\n";
        if(endpoints.size()<2)std::wcout<<L"A second active render endpoint is required for capture-to-render operation.\n";
    } else if(argc>=4 && (std::wstring(argv[1])==L"--run"||std::wstring(argv[1])==L"--run-id")) {
        std::size_t captureIndex=0,renderIndex=0;
        const bool byId=std::wstring(argv[1])==L"--run-id";
        bool endpointsValid=false;
        if(byId){
            const std::wstring sourceId=argv[2],outputId=argv[3];
            auto source=std::find_if(endpoints.begin(),endpoints.end(),[&](const Endpoint& ep){return ep.id==sourceId;});
            auto output=std::find_if(endpoints.begin(),endpoints.end(),[&](const Endpoint& ep){return ep.id==outputId;});
            if(source!=endpoints.end()&&output!=endpoints.end()){captureIndex=static_cast<std::size_t>(source-endpoints.begin());renderIndex=static_cast<std::size_t>(output-endpoints.begin());endpointsValid=true;}
        }else endpointsValid=parseIndex(argv[2],captureIndex)&&parseIndex(argv[3],renderIndex)&&captureIndex<endpoints.size()&&renderIndex<endpoints.size();
        if(!endpointsValid) {
            std::wcerr<<(byId?L"Endpoint id not found or inactive.\n":L"Invalid endpoint index. Run --list first.\n");result=2;
        } else {
            std::wstring controlName,stopName;
            bool valid=true;
            for(int i=4;i<argc;++i){
                if(std::wstring(argv[i])==L"--control-map"&&i+1<argc)controlName=argv[++i];
                else if(std::wstring(argv[i])==L"--stop-event"&&i+1<argc)stopName=argv[++i];
                else valid=false;
            }
            if(!valid){std::wcerr<<L"Invalid route options.\n";CoUninitialize();return 2;}
            HANDLE mapping=nullptr;sonicsync::win::ControlBlock* controls=nullptr;
            if(!controlName.empty()){
                mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,controlName.c_str());
                if(!mapping){std::wcerr<<L"Could not open live route controls.\n";CoUninitialize();return 2;}
                controls=static_cast<sonicsync::win::ControlBlock*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(*controls)));
                if(!controls){CloseHandle(mapping);std::wcerr<<L"Could not map live route controls.\n";CoUninitialize();return 2;}
            }
            if(!stopName.empty())g_stopEvent=OpenEventW(SYNCHRONIZE,FALSE,stopName.c_str());
            if(!stopName.empty()&&!g_stopEvent){if(controls)UnmapViewOfFile(controls);if(mapping)CloseHandle(mapping);std::wcerr<<L"Could not open route stop event.\n";CoUninitialize();return 2;}
            SetConsoleCtrlHandler(consoleHandler,TRUE);
            hr=runBridge(endpoints[captureIndex],endpoints[renderIndex],controls);
            if(FAILED(hr)&&controls)sonicsync::win::store(&controls->routeError,static_cast<LONG>(hr));
            if(controls)UnmapViewOfFile(controls);if(mapping)CloseHandle(mapping);if(g_stopEvent)CloseHandle(g_stopEvent);
            if(FAILED(hr)){std::wcerr<<L"WASAPI bridge stopped with HRESULT 0x"<<std::hex<<hr<<L"\n";result=3;}
        }
    } else {printUsage();result=2;}
    CoUninitialize();
    return result;
}

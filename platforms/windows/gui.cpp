#define NOMINMAX
#include "dsp.hpp"
#include "eq_control.hpp"
#include "artwork.hpp"
#include "resource.h"
#include "local_library.hpp"
#include "studio_session.hpp"
#include <windows.h>
#include <windowsx.h>
#include <sddl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib,"gdiplus.lib")
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace {
constexpr int kSidebar=216,kTimer=1,kVisualRefreshMs=100;
constexpr COLORREF kBg=RGB(8,8,8),kSide=RGB(20,20,20),kSurface=RGB(20,20,20),kSurface2=RGB(28,28,28),kText=RGB(245,245,245),kMuted=RGB(150,150,150),kAccent=RGB(212,175,112),kGoldSoft=RGB(226,197,142),kGoldDim=RGB(151,124,77),kGreen=RGB(143,190,151);
constexpr UINT WM_MEDIA_REFRESH=WM_APP+1;
constexpr const wchar_t* kPresetNames[]{L"Neutral",L"Music",L"Hip-hop",L"R&B",L"Classical",L"Acoustic",L"Rock",L"Movies",L"Gaming",L"Voice"};
constexpr const char* kPresetIds[]{"neutral","music","hip-hop","rnb","classical","acoustic","rock","movies","gaming","voice"};
enum class Action{Nav,Add,Play,Stop,Next,Remove,Profile,Bypass,Endpoint,SourceEndpoint,RouteToggle,SliderBass,SliderMid,SliderTreble,Song,LibraryPage,RefreshDevices};
struct Hit{RECT r;Action action;int index;};
struct Endpoint{std::wstring id,name;bool isDefault{};};
struct Metadata{std::wstring title,artist,album,source,state;sonicsync::windows::ArtworkPtr artwork;bool available{};};
struct App{
    HWND window{};ULONG_PTR gdiplus{};std::vector<Hit> hits;sonicsync::win::LocalLibrary library;std::vector<Endpoint>endpoints;
    HANDLE process{},stopEvent{},playEvent{},controlMapping{},mediaStop{},mediaRefresh{};sonicsync::win::ControlBlock*controls{};
    HANDLE routeProcess{},routeStopEvent{},routeControlMapping{};sonicsync::win::ControlBlock*routeControls{};
    std::wstring stopName,controlName,routeStopName,routeControlName,status{L"Add a WAV file to start local playback"};
    int page{},selectedEndpoint{},routeSourceEndpoint{-1}; sonicsync::EqState eq; LONG eqRate{48000};
    std::uint64_t paintCount{},fullInvalidations{},visualInvalidations{},lastPaintLog{};
    LONG lastSpectrum[16]{};LONG lastRouteState{-1};bool spectrumSnapshotValid{};
    std::atomic<std::uint64_t> mediaGeneration{0};
    int dragSlider{-1};std::atomic<bool>mediaStopping{false};std::thread mediaThread;std::mutex mediaMutex;Metadata metadata;
}app;
void invalidateFull(HWND hwnd);
RECT spectrumBounds(int width, int height);
int currentLibraryRows();
int dip(HWND,int);
int pageHeight();

sonicsync::win::StudioSession openedSession;
fs::path sessionOverride;

fs::path sessionPath() {
    if(!sessionOverride.empty()) return sessionOverride;
    PWSTR local{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        throw std::runtime_error("Cannot locate local application data");
    fs::path folder(local);
    CoTaskMemFree(local);
    return folder / L"SonicSync" / L"studio-session.txt";
}

void restoreStudio() {
    try {
        openedSession = sonicsync::win::loadSession(sessionPath());
        if (!openedSession.eq.empty()) {
            if(!sonicsync::deserializeEq(openedSession.eq,app.eq)) throw std::runtime_error("Invalid EQ state");
        } else {
            for(int i=0;i<10;++i) if(sonicsync::catalog[i].id==openedSession.profile) app.eq.select(i);
            const int offsets[]{openedSession.bass,openedSession.mid,openedSession.treble};
            for(int i=0;i<3;++i) if(offsets[i]) app.eq.setTone(i,app.eq.toneValue(i)+offsets[i]);
            app.eq.bypass=openedSession.bypass;
        }
        app.library.songs = openedSession.songs;
        app.library.selected = openedSession.selected;
        if (!app.library.songs.empty()) {
            app.library.page = std::max(0, app.library.selected) / currentLibraryRows();
            app.status = L"Library restored · press Play when ready";
        }
    } catch (const std::exception&) {
        app.status = L"Could not read the saved library and EQ settings";
    }
}

bool saveStudioBeforeClose(HWND window) {
    try {
        sonicsync::win::StudioSession current;
        current.eq=sonicsync::serializeEq(app.eq);
        current.selected=app.library.selected;
        current.songs=app.library.songs;
        if (current != openedSession) sonicsync::win::saveSession(sessionPath(), current);
        return true;
    } catch (const std::exception&) {
        return MessageBoxW(window, L"Could not save the library and EQ settings. Close without saving?",
            L"SonicSync", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }
}

Gdiplus::Color col(COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));}
void roundRect(Gdiplus::Graphics&g,RECT r,int radius,COLORREF color){Gdiplus::GraphicsPath p;const float x=static_cast<float>(r.left),y=static_cast<float>(r.top),w=static_cast<float>(r.right-r.left),h=static_cast<float>(r.bottom-r.top),d=static_cast<float>(radius*2);p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();Gdiplus::SolidBrush b(col(color));g.FillPath(&b,&p);}
void text(Gdiplus::Graphics&g,const std::wstring&s,float x,float y,float w,float h,float size,COLORREF color,bool bold=false,Gdiplus::StringAlignment align=Gdiplus::StringAlignmentNear){Gdiplus::Font f(L"Segoe UI",size,bold?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush b(col(color));Gdiplus::StringFormat fmt;fmt.SetAlignment(align);fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);Gdiplus::RectF r(x,y,w,h);g.DrawString(s.c_str(),static_cast<INT>(s.size()),&f,r,&fmt,&b);}
void line(Gdiplus::Graphics&g,float x1,float y1,float x2,float y2,COLORREF color,float width=1){Gdiplus::Pen p(col(color),width);g.DrawLine(&p,x1,y1,x2,y2);}
void line(Gdiplus::Graphics&g,int x1,int y1,int x2,int y2,COLORREF color,float width=1){line(g,static_cast<float>(x1),static_cast<float>(y1),static_cast<float>(x2),static_cast<float>(y2),color,width);}
void drawBrandMark(Gdiplus::Graphics&g,float x,float y,float size){const auto state=g.Save();g.TranslateTransform(x,y);g.ScaleTransform(size/256.0f,size/256.0f);Gdiplus::GraphicsPath p;p.AddBezier(190,57,145,20,95,36,75,65);p.AddBezier(75,65,50,101,84,130,129,144);p.AddBezier(129,144,169,156,211,172,183,209);p.AddBezier(183,209,155,246,91,229,57,198);Gdiplus::Pen gold(col(kAccent),15);gold.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);gold.SetLineJoin(Gdiplus::LineJoinRound);g.DrawPath(&gold,&p);Gdiplus::Pen echo(col(kGoldSoft),5);echo.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);g.DrawBezier(&echo,50,110,42,92,49,71,65,57);g.DrawBezier(&echo,206,145,213,163,206,183,191,197);g.Restore(state);}
void hit(RECT r,Action a,int i=0){app.hits.push_back({r,a,i});}
void button(Gdiplus::Graphics&g,RECT r,const std::wstring&label,Action a,int i=0,bool active=false,bool primary=false){roundRect(g,r,9,primary?kAccent:(active?kSurface2:kSurface));if(active&&!primary){Gdiplus::SolidBrush marker(col(kAccent));const int markerHeight=std::max(8,static_cast<int>(r.bottom-r.top)-18);g.FillRectangle(&marker,static_cast<float>(r.left+7),static_cast<float>(r.top+9),2.0f,static_cast<float>(markerHeight));}text(g,label,static_cast<float>(r.left+12),static_cast<float>(r.top),static_cast<float>(r.right-r.left-24),static_cast<float>(r.bottom-r.top),14,primary?kBg:(active?kAccent:kText),true);hit(r,a,i);}
void smallLabel(Gdiplus::Graphics&g,const std::wstring&s,int x,int y,int w){text(g,s,static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),18,11,kMuted,true);}
void addLine(HDC dc,int x1,int y1,int x2,int y2,COLORREF c){HPEN p=CreatePen(PS_SOLID,1,c);auto old=SelectObject(dc,p);MoveToEx(dc,x1,y1,nullptr);LineTo(dc,x2,y2);SelectObject(dc,old);DeleteObject(p);}
std::wstring quote(const std::wstring&v){std::wstring o=L"\"";std::size_t n=0;for(wchar_t c:v){if(c==L'\\'){++n;continue;}if(c==L'\"'){o.append(n*2+1,L'\\');o.push_back(c);n=0;continue;}o.append(n,L'\\');n=0;o.push_back(c);}o.append(n*2,L'\\');o.push_back(L'\"');return o;}
bool enumerateEndpoints(std::vector<Endpoint>&out){ComPtr<IMMDeviceEnumerator>e;if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e))))return false;ComPtr<IMMDevice>def;std::wstring did;if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&def))){LPWSTR x=nullptr;if(SUCCEEDED(def->GetId(&x))){did=x;CoTaskMemFree(x);}}ComPtr<IMMDeviceCollection>list;if(FAILED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&list)))return false;UINT n=0;if(FAILED(list->GetCount(&n)))return false;for(UINT i=0;i<n;++i){ComPtr<IMMDevice>d;if(FAILED(list->Item(i,&d)))continue;LPWSTR id=nullptr;if(FAILED(d->GetId(&id)))continue;Endpoint x;x.id=id;CoTaskMemFree(id);x.isDefault=x.id==did;ComPtr<IPropertyStore>s;if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&s))){PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(s->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)x.name=v.pwszVal;PropVariantClear(&v);}if(x.name.empty())x.name=L"Audio output";if(x.isDefault)x.name+=L" · default";out.push_back(std::move(x));}return !out.empty();}
const wchar_t* profileName() { return app.eq.profile<0 ? L"Custom" : kPresetNames[app.eq.profile]; }
void publishTo(sonicsync::win::ControlBlock* c) {
    if(!c) return;
    const LONG rate=sonicsync::win::load(&c->sampleRate);
    app.eqRate=rate>=8000 ? rate : 48000;
    const auto preset=sonicsync::makeEqPreset(app.eq,app.eqRate);
    sonicsync::win::EqCommand message{app.eq.profile,app.eq.bypass,static_cast<LONG>(std::lround(preset.preampDb*1000))};
    for(int i=0;i<10;++i) message.gains[i]=app.eq.effective(i);
    sonicsync::win::publishEq(*c,message);
}
void publish(){publishTo(app.controls);publishTo(app.routeControls);}
void closePlayerHandles(){if(app.controls){UnmapViewOfFile(app.controls);app.controls=nullptr;}if(app.controlMapping){CloseHandle(app.controlMapping);app.controlMapping=nullptr;}if(app.stopEvent){CloseHandle(app.stopEvent);app.stopEvent=nullptr;}if(app.playEvent){CloseHandle(app.playEvent);app.playEvent=nullptr;}app.stopName.clear();app.controlName.clear();}
void stopPlayer(){if(app.stopEvent)SetEvent(app.stopEvent);if(app.process){if(WaitForSingleObject(app.process,1800)==WAIT_TIMEOUT){TerminateProcess(app.process,0);WaitForSingleObject(app.process,500);}CloseHandle(app.process);app.process=nullptr;}closePlayerHandles();}
bool playbackPaused() {
    return app.playEvent && WaitForSingleObject(app.playEvent, 0) == WAIT_TIMEOUT;
}

HANDLE createEqMapping(const std::wstring& name) {
    HANDLE token{};
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD bytes{};
    GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> data(bytes);
    const bool ok=bytes && GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes);
    CloseHandle(token);
    if(!ok) return nullptr;
    LPWSTR sid{};
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid)) return nullptr;
    const std::wstring dacl=std::wstring(L"D:P(A;;GA;;;")+sid+L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{};
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))
        return nullptr;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,
                                      sizeof(sonicsync::win::ControlBlock),name.c_str());
    if(mapping && GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(mapping); mapping=nullptr; }
    LocalFree(descriptor);
    return mapping;
}
bool startPlayer(int ix){if(app.routeProcess){app.status=L"Stop external routing before starting local playback";return false;}if(ix<0||ix>=static_cast<int>(app.library.songs.size()))return false;if(app.selectedEndpoint<0||app.selectedEndpoint>=static_cast<int>(app.endpoints.size())){app.status=L"Choose an available output device";return false;}GUID guid{};if(FAILED(CoCreateGuid(&guid)))return false;wchar_t id[40]{};StringFromGUID2(guid,id,40);app.stopName=L"Local\\SonicSyncStop"+std::wstring(id);app.controlName=L"Local\\SonicSyncControl"+std::wstring(id);app.stopEvent=CreateEventW(nullptr,TRUE,FALSE,app.stopName.c_str());if(!app.stopEvent){app.status=L"Could not create the playback stop control";return false;}const std::wstring playName = app.stopName + L"-playing";
    app.playEvent = CreateEventW(nullptr, TRUE, TRUE, playName.c_str());
    if (!app.playEvent) {
        closePlayerHandles();
        app.status = L"Could not create the pause control";
        return false;
    }
    app.controlMapping=createEqMapping(app.controlName);if(!app.controlMapping){closePlayerHandles();app.status=L"Could not create the live EQ control channel";return false;}app.controls=static_cast<sonicsync::win::ControlBlock*>(MapViewOfFile(app.controlMapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(sonicsync::win::ControlBlock)));if(!app.controls){closePlayerHandles();app.status=L"Could not map the live EQ controls";return false;}publish();wchar_t self[MAX_PATH]{};const DWORD n=GetModuleFileNameW(nullptr,self,MAX_PATH);if(!n||n>=MAX_PATH){closePlayerHandles();return false;}const fs::path player=fs::path(self).parent_path()/L"sonicsync_player.exe";std::wstring cmd=quote(player.wstring())+L" "+quote(fs::absolute(app.library.songs[static_cast<std::size_t>(ix)]).wstring())+L" --device-id "+quote(app.endpoints[static_cast<std::size_t>(app.selectedEndpoint)].id)+L" --stop-event "+quote(app.stopName)+L" --play-event "+quote(playName)+L" --control-map "+quote(app.controlName);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessW(player.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,fs::current_path().c_str(),&si,&pi)){closePlayerHandles();app.status=L"Playback engine could not start";return false;}CloseHandle(pi.hThread);app.process=pi.hProcess;app.library.current=ix;app.library.selected=ix;app.library.page=ix/currentLibraryRows();app.status = (app.eq.bypass ? L"WAV playback · bypass · " : L"WAV playback · EQ active · ") + app.endpoints[app.selectedEndpoint].name;return true;}
void closeRouteHandles(){if(app.routeControls){UnmapViewOfFile(app.routeControls);app.routeControls=nullptr;}if(app.routeControlMapping){CloseHandle(app.routeControlMapping);app.routeControlMapping=nullptr;}if(app.routeStopEvent){CloseHandle(app.routeStopEvent);app.routeStopEvent=nullptr;}app.routeControlName.clear();app.routeStopName.clear();}
void stopRoute(){if(app.routeStopEvent)SetEvent(app.routeStopEvent);if(app.routeProcess){if(WaitForSingleObject(app.routeProcess,1200)==WAIT_TIMEOUT){TerminateProcess(app.routeProcess,0);WaitForSingleObject(app.routeProcess,500);}CloseHandle(app.routeProcess);app.routeProcess=nullptr;}closeRouteHandles();}
bool startRoute(){if(app.process){app.status=L"Stop local playback before starting external routing";return false;}if(app.routeSourceEndpoint<0||app.routeSourceEndpoint>=static_cast<int>(app.endpoints.size())||app.selectedEndpoint<0||app.selectedEndpoint>=static_cast<int>(app.endpoints.size())){app.status=L"Choose a source and physical output endpoint";return false;}if(app.routeSourceEndpoint==app.selectedEndpoint){app.status=L"Source and physical output must be different endpoints";return false;}GUID guid{};if(FAILED(CoCreateGuid(&guid)))return false;wchar_t id[40]{};StringFromGUID2(guid,id,40);app.routeStopName=L"Local\\SonicSyncRouteStop"+std::wstring(id);app.routeControlName=L"Local\\SonicSyncRouteControl"+std::wstring(id);app.routeStopEvent=CreateEventW(nullptr,TRUE,FALSE,app.routeStopName.c_str());if(!app.routeStopEvent){app.status=L"Could not create route stop control";return false;}app.routeControlMapping=createEqMapping(app.routeControlName);if(!app.routeControlMapping){closeRouteHandles();app.status=L"Could not create route control channel";return false;}app.routeControls=static_cast<sonicsync::win::ControlBlock*>(MapViewOfFile(app.routeControlMapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(sonicsync::win::ControlBlock)));if(!app.routeControls){closeRouteHandles();app.status=L"Could not map route controls";return false;}publish();wchar_t self[MAX_PATH]{};const DWORD n=GetModuleFileNameW(nullptr,self,MAX_PATH);if(!n||n>=MAX_PATH){closeRouteHandles();return false;}const fs::path bridge=fs::path(self).parent_path()/L"sonicsync_wasapi.exe";std::wstring cmd=quote(bridge.wstring())+L" --run-id "+quote(app.endpoints[static_cast<std::size_t>(app.routeSourceEndpoint)].id)+L" "+quote(app.endpoints[static_cast<std::size_t>(app.selectedEndpoint)].id)+L" --control-map "+quote(app.routeControlName)+L" --stop-event "+quote(app.routeStopName);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessW(bridge.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,fs::current_path().c_str(),&si,&pi)){closeRouteHandles();app.status=L"External routing engine could not start";return false;}CloseHandle(pi.hThread);app.routeProcess=pi.hProcess;app.status=L"Starting routed endpoint…";return true;}
int libraryRows(int height) { return std::max(1, (height - 218) / 54); }

int currentLibraryRows() {
    RECT client{};
    GetClientRect(app.window, &client);
    return libraryRows(std::max(pageHeight(),dip(app.window,client.bottom)));
}

void addFiles() {
    std::vector<wchar_t> paths(65536);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0\0";
    dialog.lpstrFile = paths.data();
    dialog.nMaxFile = static_cast<DWORD>(paths.size());
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Add local WAV files";
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError()) app.status = L"Could not read that selection. Try choosing fewer files.";
        return;
    }
    const fs::path first = paths.data();
    const wchar_t* name = paths.data() + first.native().size() + 1;
    const int before = app.library.count();
    if (!*name) app.library.add(first);
    else {
        while (*name) {
            app.library.add(first / name);
            name += wcslen(name) + 1;
        }
    }
    const int added = app.library.count() - before;
    // Start a batch at its first new track so Play follows import order.
    if (added) app.library.selected = before;
    app.library.page = app.library.selected / currentLibraryRows();
    app.page = 2;
    app.status = added ? std::to_wstring(added) + (added == 1 ? L" track added" : L" tracks added")
                       : L"Already in your library";
}

void metadataThread() noexcept {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto request = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        if (request.wait_for(std::chrono::seconds(3)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
            winrt::uninit_apartment();
            return;
        }
        auto manager = request.GetResults();
        const auto sessionToken = manager.CurrentSessionChanged([](auto const&, auto const&) {
            app.mediaGeneration.fetch_add(1, std::memory_order_relaxed);
            if (app.mediaRefresh) SetEvent(app.mediaRefresh);
        });
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession watchedSession{nullptr};
        winrt::event_token propertiesToken{};
        std::wstring attemptedArtworkKey;
        auto lastArtworkAttempt = std::chrono::steady_clock::time_point{};
        while (!app.mediaStopping.load()) {
            const auto generation = app.mediaGeneration.load(std::memory_order_relaxed);
            Metadata next;
            auto session = manager.GetCurrentSession();
            if (session != watchedSession) {
                if (watchedSession) watchedSession.MediaPropertiesChanged(propertiesToken);
                watchedSession = session;
                if (watchedSession) propertiesToken = watchedSession.MediaPropertiesChanged([](auto const&, auto const&) {
                    app.mediaGeneration.fetch_add(1, std::memory_order_relaxed);
                    if (app.mediaRefresh) SetEvent(app.mediaRefresh);
                });
            }
            if (session) {
                next.available = true;
                const auto appId = session.SourceAppUserModelId();
                next.source = appId.empty() ? L"Windows media session" : std::wstring(appId);
                const auto playback = session.GetPlaybackInfo().PlaybackStatus();
                next.state = playback == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ? L"Paused" :
                    (playback == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? L"Playing" : L"Media session");
                auto properties = session.TryGetMediaPropertiesAsync();
                if (properties.wait_for(std::chrono::seconds(2)) == winrt::Windows::Foundation::AsyncStatus::Completed) {
                    auto media = properties.GetResults();
                    next.title = std::wstring(media.Title());
                    next.artist = std::wstring(media.Artist());
                    next.album = std::wstring(media.AlbumTitle());
                    const std::wstring artworkKey = next.source + L"\x1f" + next.title + L"\x1f" + next.artist + L"\x1f" + next.album;
                    bool sameCurrentTrack = false;
                    {
                        std::lock_guard<std::mutex> lock(app.mediaMutex);
                        sameCurrentTrack = next.source == app.metadata.source && next.title == app.metadata.title &&
                            next.artist == app.metadata.artist && next.album == app.metadata.album;
                        if (sameCurrentTrack) next.artwork = app.metadata.artwork;
                    }
                    const bool currentHasArtwork = static_cast<bool>(next.artwork);
                    const auto now = std::chrono::steady_clock::now();
                    const bool retryMissing = !currentHasArtwork && now - lastArtworkAttempt >= std::chrono::seconds(10);
                    if (!artworkKey.empty() && (artworkKey != attemptedArtworkKey || retryMissing)) {
                        attemptedArtworkKey = artworkKey;
                        lastArtworkAttempt = now;
                        try {
                            auto thumbnail = media.Thumbnail();
                            if (thumbnail) {
                                auto read = thumbnail.OpenReadAsync();
                                if (read.wait_for(std::chrono::seconds(2)) == winrt::Windows::Foundation::AsyncStatus::Completed) {
                                    auto input = read.GetResults();
                                    const auto length = input.Size();
                                    if (length > 0 && length <= 8U * 1024U * 1024U) {
                                        winrt::Windows::Storage::Streams::DataReader reader(input.GetInputStreamAt(0));
                                        auto loaded = reader.LoadAsync(static_cast<std::uint32_t>(length));
                                        if (loaded.wait_for(std::chrono::seconds(2)) == winrt::Windows::Foundation::AsyncStatus::Completed) {
                                            const auto count = loaded.GetResults();
                                            if (count > 0) {
                                                std::vector<std::uint8_t> encoded(count);
                                                reader.ReadBytes(encoded);
                                                next.artwork = sonicsync::windows::decodeArtwork(encoded.data(), encoded.size());
                                            }
                                        }
                                    }
                                }
                            }
                        } catch (...) { next.artwork.reset(); }
                    }
                }
            }
            if (app.mediaGeneration.load(std::memory_order_relaxed) != generation) continue;
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(app.mediaMutex);
                changed = next.title != app.metadata.title || next.artist != app.metadata.artist || next.album != app.metadata.album ||
                    next.source != app.metadata.source || next.state != app.metadata.state || next.available != app.metadata.available ||
                    next.artwork != app.metadata.artwork;
                app.metadata = std::move(next);
            }
            if (changed) PostMessageW(app.window, WM_MEDIA_REFRESH, 0, 0);
            const HANDLE waits[]{app.mediaStop, app.mediaRefresh};
            if (WaitForMultipleObjects(2, waits, FALSE, 1800) == WAIT_OBJECT_0) break;
        }
        if (watchedSession) watchedSession.MediaPropertiesChanged(propertiesToken);
        manager.CurrentSessionChanged(sessionToken);
        winrt::uninit_apartment();
    } catch (...) { }
}

bool drawMediaArtwork(Gdiplus::Graphics& g, const Metadata& metadata, const Gdiplus::GraphicsPath& clip, int x, int y, int size) {
    if (!metadata.artwork || metadata.artwork->bgra.empty()) return false;
    Gdiplus::Bitmap bitmap(static_cast<INT>(metadata.artwork->width), static_cast<INT>(metadata.artwork->height),
                           static_cast<INT>(metadata.artwork->width * 4U), PixelFormat32bppARGB,
                           const_cast<BYTE*>(metadata.artwork->bgra.data()));
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;
    const auto state = g.Save();
    g.SetClip(&clip, Gdiplus::CombineModeIntersect);
    const auto status = g.DrawImage(&bitmap, x, y, size, size);
    g.Restore(state);
    return status == Gdiplus::Ok;
}

void drawSpectrum(Gdiplus::Graphics&g,int x,int y,int w,int h){RECT r{x,y,x+w,y+h};roundRect(g,r,12,kSurface);text(g,L"LIVE SPECTRUM",static_cast<float>(x+18),static_cast<float>(y+12),160,18,11,kMuted,true);text(g,app.routeProcess?L"Live routed endpoint spectrum":L"Signal from SonicSync WAV output",static_cast<float>(x+w-250),static_cast<float>(y+12),230,18,11,kMuted,false,Gdiplus::StringAlignmentFar);auto* spectrumControls=app.routeProcess?app.routeControls:app.controls;LONG bins[16]{};if(spectrumControls)for(int i=0;i<16;++i)bins[i]=sonicsync::win::load(&spectrumControls->spectrumMilli[i]);const int base=y+h-24,top=y+42,innerW=w-36;const float gap=5.0f,bw=(static_cast<float>(innerW)-gap*15)/16;for(int i=0;i<16;++i){float v=spectrumControls?std::clamp(static_cast<float>(bins[i])/1000.0f,0.0f,1.0f):0.0f;const float bh=std::max(3.0f,v*static_cast<float>(base-top));Gdiplus::RectF bar(static_cast<float>(x+18)+static_cast<float>(i)*(bw+gap),static_cast<float>(base)-bh,bw,bh);Gdiplus::SolidBrush brush(i<5?col(kAccent):col(kGoldDim));g.FillRectangle(&brush,bar);}if(!spectrumControls)text(g,L"Start local or routed playback to see its measured spectrum",static_cast<float>(x+20),static_cast<float>(y+48),static_cast<float>(w-40),28,12,kMuted);}

void drawNav(Gdiplus::Graphics&g,int h){RECT side{0,0,kSidebar,h};Gdiplus::SolidBrush sb(col(kSide));g.FillRectangle(&sb,0,0,kSidebar,h);drawBrandMark(g,24.0f,20.0f,40.0f);text(g,L"SONICSYNC",70.0f,24.0f,130.0f,34.0f,16,kText,true);text(g,L"AUDIO STUDIO",70.0f,49.0f,130.0f,18.0f,9,kMuted,true);line(g,20,82,kSidebar-20,82,kSurface2);const wchar_t* nav[]{L"Now Playing",L"Equalizer",L"Library",L"Devices"};for(int i=0;i<4;++i){RECT r{12,102+i*48,kSidebar-12,144+i*48};if(app.page==i){roundRect(g,r,9,kSurface2);Gdiplus::SolidBrush marker(col(kAccent));g.FillRectangle(&marker,static_cast<float>(r.left+7),static_cast<float>(r.top+11),2.0f,20.0f);}text(g,nav[i],30.0f,static_cast<float>(r.top),static_cast<float>(r.right-r.left-36),42.0f,14,app.page==i?kAccent:kMuted,app.page==i);hit(r,Action::Nav,i);}line(g,20,h-72,kSidebar-20,h-72,kSurface2);text(g,L"LOCAL AUDIO",25.0f,static_cast<float>(h-59),170.0f,16.0f,9,kMuted,true);text(g,L"WAV route · this app only",25.0f,static_cast<float>(h-38),175.0f,22.0f,11,kMuted);}

void drawHome(Gdiplus::Graphics&g,int w,int h){const int x=kSidebar+40,content=w-x-34;smallLabel(g,L"NOW PLAYING",x,24,180);text(g,L"Sound, in focus.",static_cast<float>(x),45,500,40,28,kText,true);
    Metadata m;
    if (app.process && app.library.current >= 0 && app.library.current < app.library.count()) {
        m.title = app.library.songs[static_cast<std::size_t>(app.library.current)].stem().wstring();
        m.artist = L"Local WAV file";
        m.source = L"SonicSync WAV player";
        m.state = playbackPaused() ? L"Paused" : L"Playing";
    } else {
        std::lock_guard<std::mutex> lock(app.mediaMutex);
        m = app.metadata;
    }
    const bool ext = !app.process && m.available;
    const std::wstring title = m.title.empty() ? L"Nothing playing" : m.title;
    const std::wstring artist = !m.artist.empty() ? m.artist : (ext ? L"Artist unavailable" : L"Choose a track from your library");
    const std::wstring& album = m.album;
    const std::wstring& source = m.source;
    const int art=std::clamp((content-340)/2,230,300),ay=114;RECT artR{x,ay,x+art,ay+art};Gdiplus::LinearGradientBrush grad(Gdiplus::Rect(artR.left,artR.top,art,art),Gdiplus::Color(255,30,30,30),Gdiplus::Color(255,20,20,20),45);Gdiplus::GraphicsPath path;path.AddArc(Gdiplus::RectF(static_cast<float>(x),static_cast<float>(ay),26.0f,26.0f),180.0f,90.0f);path.AddArc(Gdiplus::RectF(static_cast<float>(x+art-26),static_cast<float>(ay),26.0f,26.0f),270.0f,90.0f);path.AddArc(Gdiplus::RectF(static_cast<float>(x+art-26),static_cast<float>(ay+art-26),26.0f,26.0f),0.0f,90.0f);path.AddArc(Gdiplus::RectF(static_cast<float>(x),static_cast<float>(ay+art-26),26.0f,26.0f),90.0f,90.0f);path.CloseFigure();g.FillPath(&grad,&path);if(!drawMediaArtwork(g,m,path,x,ay,art)){drawBrandMark(g,static_cast<float>(x+art/4),static_cast<float>(ay+art/6),static_cast<float>(art/2));text(g,L"NO COVER AVAILABLE",static_cast<float>(x+12),static_cast<float>(ay+art-34),static_cast<float>(art-24),20,9,kMuted,true,Gdiplus::StringAlignmentCenter);}
    const int tx=x+art+30,tw=std::max(260,content-art-30);smallLabel(g,ext?L"WINDOWS MEDIA SESSION":L"SONICSYNC PLAYER",tx,ay+4,tw);text(g,title,static_cast<float>(tx),static_cast<float>(ay+34),static_cast<float>(tw),64,27,kText,true);text(g,artist,static_cast<float>(tx),static_cast<float>(ay+100),static_cast<float>(tw),32,17,RGB(217,217,217));text(g,album.empty()?L"Album unavailable":album,static_cast<float>(tx),static_cast<float>(ay+132),static_cast<float>(tw),26,13,kMuted);text(g,source.empty()?L"Source unavailable":source,static_cast<float>(tx),static_cast<float>(ay+164),static_cast<float>(tw),22,11,kMuted);text(g,m.state.empty()?L"Ready":m.state,static_cast<float>(tx),static_cast<float>(ay+196),static_cast<float>(tw),24,12,ext?kGreen:kMuted,true);
    RECT profile{x,ay+art+18,x+art,ay+art+76};roundRect(g,profile,12,kSurface);smallLabel(g,L"ACTIVE PROFILE",x+16,profile.top+8,150);text(g,profileName(),static_cast<float>(x+16),static_cast<float>(profile.top+30),static_cast<float>(art-86),28,17,kAccent,true);button(g,RECT{x+art-92,profile.top+12,x+art-10,profile.top+48},L"Edit",Action::Nav,1);
    const bool routeAlive=app.routeProcess&&WaitForSingleObject(app.routeProcess,0)==WAIT_TIMEOUT;const LONG routeState=app.routeControls?sonicsync::win::load(&app.routeControls->routeStatus):0;const bool routeActive=routeAlive&&routeState==2;RECT proc{tx,ay+226,tx+tw,ay+280};roundRect(g,proc,11,kSurface);const bool audioActive=(app.process&&!playbackPaused())||routeActive;Gdiplus::SolidBrush dot(col(audioActive&&!app.eq.bypass?kGreen:kMuted));g.FillEllipse(&dot,static_cast<float>(tx+15),static_cast<float>(proc.top+20),9.0f,9.0f);const std::wstring status=playbackPaused()?L"Local playback paused":app.eq.bypass?L"Bypass · original signal":app.process?L"Processing local WAV audio":routeActive?L"Processing external endpoint audio":routeAlive?L"External route ready · waiting for audio":ext?L"Metadata detected · external audio is not routed":L"Waiting for local WAV playback";text(g,status,static_cast<float>(tx+34),static_cast<float>(proc.top+8),static_cast<float>(tw-48),34,12,audioActive&&!app.eq.bypass?kGreen:kMuted,true);
    if(app.process||routeActive){const int index=app.selectedEndpoint;if(index>=0&&index<static_cast<int>(app.endpoints.size()))text(g,app.endpoints[static_cast<std::size_t>(index)].name,static_cast<float>(tx),static_cast<float>(proc.bottom+8),static_cast<float>(tw),20,10,kMuted);}
    const RECT spectrum = spectrumBounds(w,h);
    drawSpectrum(g,spectrum.left,spectrum.top,spectrum.right-spectrum.left,spectrum.bottom-spectrum.top);
    const int cy=h-108;line(g,x,cy-18,w-32,cy-18,kSurface2);button(g,RECT{x,cy,w-32,cy+44},app.eq.bypass?L"Bypass ON  ·  click to restore EQ":std::wstring(L"Processing ON · ")+profileName(),Action::Bypass,0,app.eq.bypass,app.process!=nullptr||app.routeProcess!=nullptr);
    const int by=cy+52;button(g,RECT{x,by,x+116,by+34},L"Add WAV",Action::Add,0,false,true);button(g,RECT{x+126,by,x+216,by+34},app.process?(playbackPaused()?L"Resume":L"Pause"):L"Play",Action::Play);button(g,RECT{x+226,by,x+316,by+34},L"Stop",Action::Stop);button(g,RECT{x+326,by,x+416,by+34},L"Next",Action::Next);text(g,app.status,static_cast<float>(x+432),static_cast<float>(by),static_cast<float>(content-432),34,11,kMuted);
}

void drawGraph(Gdiplus::Graphics&g,int x,int y,int w,int h){RECT r{x,y,x+w,y+h};roundRect(g,r,14,kSurface);for(int i=0;i<=4;++i){const float yy=static_cast<float>(y+24)+static_cast<float>(i)*(h-48)/4.0f;line(g,x+20,static_cast<int>(yy),x+w-18,static_cast<int>(yy),kSurface2);}const auto preset=sonicsync::makeEqPreset(app.eq,app.eqRate);Gdiplus::PointF points[96];for(int i=0;i<96;++i){const double f=20.0*std::pow(1000.0,static_cast<double>(i)/95.0);const double db=std::clamp((app.eq.bypass?0.0:sonicsync::responseDb(preset,f,app.eqRate)),-12.0,12.0);points[i].X=static_cast<float>(x+65)+static_cast<float>(i)*(w-85)/95.0f;points[i].Y=static_cast<float>(y+h-24)-(static_cast<float>(db)+12.0f)*(h-48)/24.0f;}Gdiplus::Pen p(col(kAccent),3);g.DrawLines(&p,points,96);smallLabel(g,L"+12 dB",x+22,y+6,60);smallLabel(g,L"0 dB",x+22,y+h/2-7,60);smallLabel(g,L"−12 dB",x+22,y+h-44,60);text(g,L"20 Hz",static_cast<float>(x+20),static_cast<float>(y+h-22),55,18,9,kMuted);text(g,L"100 Hz",static_cast<float>(x+w*0.31),static_cast<float>(y+h-22),65,18,9,kMuted);text(g,L"1 kHz",static_cast<float>(x+w*0.60),static_cast<float>(y+h-22),55,18,9,kMuted);text(g,L"20 kHz",static_cast<float>(x+w-80),static_cast<float>(y+h-22),60,18,9,kMuted,false,Gdiplus::StringAlignmentFar);}
void drawSlider(Gdiplus::Graphics&g,int x,int y,int w,const wchar_t*name,int milli,Action a){smallLabel(g,name,x,y,w);const int yy=y+27;line(g,static_cast<float>(x),static_cast<float>(yy),static_cast<float>(x+w),static_cast<float>(yy),kSurface2,4);const int value=std::clamp(milli,-12000,12000);const int px=x+(value+12000)*w/24000;line(g,static_cast<float>(x+w/2),static_cast<float>(yy),static_cast<float>(px),static_cast<float>(yy),kAccent,4);Gdiplus::SolidBrush b(col(kText));g.FillEllipse(&b,static_cast<float>(px-7),static_cast<float>(yy-7),14.0f,14.0f);text(g,(value>0?std::wstring(L"+"):std::wstring())+std::to_wstring(value/1000)+L" dB",static_cast<float>(x+w-76),static_cast<float>(y-1),76,18,11,kText,false,Gdiplus::StringAlignmentFar);hit(RECT{x,y+13,x+w,y+42},a);}
void drawEq(Gdiplus::Graphics&g,int w,int h){const int x=kSidebar+40,cw=w-x-34;smallLabel(g,L"EQUALIZER",x,24,200);text(g,L"Shape your sound",static_cast<float>(x),45,520,40,28,kText,true);text(g,L"Profiles blend into playback smoothly. No Apply step.",static_cast<float>(x),84,600,22,12,kMuted);const int chipsY=118,gap=8,chipW=(cw-gap*4)/5;for(int i=0;i<10;++i){const int row=i/5,colx=i%5;RECT r{x+colx*(chipW+gap),chipsY+row*42,x+colx*(chipW+gap)+chipW,chipsY+row*42+34};button(g,r,kPresetNames[i],Action::Profile,i,i==app.eq.profile);}const int graphY=210,graphH=std::max<int>(190,static_cast<int>(h)-410);drawGraph(g,x,graphY,cw,graphH);const int sliderY=graphY+graphH+18;const int sg=22,sw=(cw-sg*2)/3;drawSlider(g,x,sliderY,sw,L"BASS",app.eq.toneValue(0),Action::SliderBass);drawSlider(g,x+sw+sg,sliderY,sw,L"MID",app.eq.toneValue(1),Action::SliderMid);drawSlider(g,x+2*(sw+sg),sliderY,sw,L"TREBLE",app.eq.toneValue(2),Action::SliderTreble);button(g,RECT{x,h-55,x+145,h-17},app.eq.bypass?L"Bypass is on":L"Bypass",Action::Bypass,0,app.eq.bypass);text(g,app.process?L"Live controls active · transition ~50 ms":app.routeProcess?L"Applied to external route":L"Saved for next playback",static_cast<float>(x+164),static_cast<float>(h-55),static_cast<float>(cw-164),38,11,kMuted);}
void drawLibrary(Gdiplus::Graphics& g, int w, int h) {
    const int x = kSidebar + 40, width = w - x - 34;
    smallLabel(g, L"LOCAL LIBRARY", x, 24, 200);
    text(g, L"Your audio", static_cast<float>(x), 45, 520, 40, 28, kText, true);
    button(g, RECT{w-180,48,w-32,88}, L"Add WAV files", Action::Add, 0, false, true);
    const int count = app.library.count();
    const int rows = libraryRows(h);
    const int last = app.library.lastPage(rows);
    app.library.page = std::clamp(app.library.page, 0, last);
    const int first = app.library.page * rows;
    const int end = std::min(count, first + rows);
    if (count) {
        const std::wstring range = L"Tracks " + std::to_wstring(first + 1) + L"–" + std::to_wstring(end) + L" of " + std::to_wstring(count);
        text(g, range, static_cast<float>(x), 98, static_cast<float>(width), 25, 12, kMuted);
    } else {
        text(g,L"No local tracks yet",static_cast<float>(x),170,static_cast<float>(width),34,18,kMuted,true);
        button(g,RECT{x,218,x+166,258},L"Choose WAV files",Action::Add,0,false,true);
    }
    for (int i = first; i < end; ++i) {
        const int y = 146 + (i - first) * 54;
        const bool selected = i == app.library.selected;
        const bool playing = app.process && i == app.library.current;
        roundRect(g, RECT{x,y,w-32,y+44}, 9, selected ? kSurface2 : kSurface);
        text(g, app.library.songs[static_cast<std::size_t>(i)].stem().wstring(), static_cast<float>(x+16),
             static_cast<float>(y), static_cast<float>(width-290), 44, 14, kText, selected);
        text(g, playing ? L"Playing" : L"WAV", static_cast<float>(w-264), static_cast<float>(y), 78, 44, 12, playing ? kGreen : kMuted);
        button(g, RECT{w-178,y+5,w-112,y+39}, L"Play", Action::Song, i, false, true);
        button(g, RECT{w-104,y+5,w-38,y+39}, L"Remove", Action::Remove, i);
    }
    const int footer = h - 58;
    if (app.library.page > 0) button(g, RECT{w-250,footer,w-142,footer+38}, L"Previous", Action::LibraryPage, -1);
    if (app.library.page < last) button(g, RECT{w-134,footer,w-32,footer+38}, L"Next", Action::LibraryPage, 1);
    text(g, app.status, static_cast<float>(x), static_cast<float>(footer), static_cast<float>(width - (last ? 230 : 0)), 38, 12, kMuted);
}
void drawDevices(Gdiplus::Graphics&g,int w,int){const int x=kSidebar+40,cw=w-x-34;smallLabel(g,L"AUDIO ROUTING",x,24,200);text(g,L"Devices & external audio",static_cast<float>(x),45,static_cast<float>(cw-180),40,28,kText,true);button(g,RECT{w-170,48,w-32,88},L"Refresh devices",Action::RefreshDevices);text(g,L"For external apps, select a virtual playback endpoint as Source, then choose your headphones/speakers as Output.",static_cast<float>(x),96,static_cast<float>(cw),30,12,kMuted);smallLabel(g,L"SOURCE · VIRTUAL PLAYBACK ENDPOINT",x,152,420);smallLabel(g,L"OUTPUT · PHYSICAL DEVICE",x+cw/2+10,152,cw/2-10);std::wstring source=app.routeSourceEndpoint>=0&&app.routeSourceEndpoint<static_cast<int>(app.endpoints.size())?app.endpoints[static_cast<std::size_t>(app.routeSourceEndpoint)].name:L"Select a virtual playback endpoint";std::wstring output=app.selectedEndpoint>=0&&app.selectedEndpoint<static_cast<int>(app.endpoints.size())?app.endpoints[static_cast<std::size_t>(app.selectedEndpoint)].name:L"Select a physical output";button(g,RECT{x,178,x+cw/2-8,230},source+L"  ›",Action::SourceEndpoint,0,false);button(g,RECT{x+cw/2+8,178,x+cw,230},output+L"  ›",Action::Endpoint,0,false);text(g,L"Use a virtual Source. A physical Source may also play unprocessed audio (double sound). Default output is unchanged.",static_cast<float>(x),238,static_cast<float>(cw),24,11,kMuted);const bool alive=app.routeProcess&&WaitForSingleObject(app.routeProcess,0)==WAIT_TIMEOUT;const LONG routeState=app.routeControls?sonicsync::win::load(&app.routeControls->routeStatus):0;const bool active=alive&&routeState==2;button(g,RECT{x,286,x+190,330},app.routeProcess?L"Stop external route":L"Start external route",Action::RouteToggle,0,active,!app.routeProcess);if(active){text(g,L"SOURCE SIGNAL IS PASSING THROUGH DSP",static_cast<float>(x+210),286,280,20,11,kGreen,true);text(g,source+L"  →  "+output,static_cast<float>(x+210),306,static_cast<float>(cw-220),24,12,kText);}else{text(g,alive&&routeState==1?L"Route ready · waiting for audio":(alive?L"Starting external route…":L"External audio processing is off"),static_cast<float>(x+210),286,static_cast<float>(cw-220),20,12,kMuted,true);text(g,L"Requires an installed virtual audio endpoint and apps routed to it.",static_cast<float>(x+210),306,static_cast<float>(cw-220),24,11,kMuted);}text(g,L"Route changes are manual. If audio stops: Stop route, set Windows output back to your headphones/speakers, then relaunch audio apps.",static_cast<float>(x),356,static_cast<float>(cw),44,12,kMuted);text(g,app.status,static_cast<float>(x),400,static_cast<float>(cw),24,12,kGoldSoft);for(std::size_t i=0;i<app.endpoints.size();++i){int y=430+static_cast<int>(i)*42;const std::wstring role=static_cast<int>(i)==app.routeSourceEndpoint?L" · source":(static_cast<int>(i)==app.selectedEndpoint?L" · output":L"");text(g,app.endpoints[i].name+role,static_cast<float>(x+6),static_cast<float>(y),static_cast<float>(cw-12),34,12,kMuted);}}
void invalidateFull(HWND hwnd){++app.fullInvalidations;InvalidateRect(hwnd,nullptr,FALSE);}
RECT spectrumBounds(int width, int height) {
    const int x = kSidebar + 40, content = width - x - 34;
    const int art = std::clamp((content - 340) / 2, 230, 300);
    const int left = x + art + 30, top = 114 + art + 106;
    const int w = std::max(260, content - art - 30);
    const int h = std::clamp(height - top - 184, 106, 148);
    return {left, top, left + w, top + h};
}

int dip(HWND w,int pixels){return MulDiv(pixels,96,static_cast<int>(GetDpiForWindow(w)));}
int px(HWND w,int logical){return MulDiv(logical,static_cast<int>(GetDpiForWindow(w)),96);}
int pageHeight(){return app.page==3?std::max(680,460+static_cast<int>(app.endpoints.size())*42):720;}
int scrollY{};
void updateScroll(HWND w,int pos){
    RECT client{};GetClientRect(w,&client);
    const int visible=dip(w,client.bottom);
    scrollY=std::clamp(pos,0,std::max(0,pageHeight()-visible));
    SCROLLINFO si{sizeof(si),SIF_PAGE|SIF_POS|SIF_RANGE,0,pageHeight()-1,static_cast<UINT>(visible),scrollY};
    SetScrollInfo(w,SB_VERT,&si,TRUE);
}
void invalidateSpectrum(HWND hwnd) {
    if(IsIconic(hwnd) || app.page!=0)return;
    RECT client{}; GetClientRect(hwnd,&client);
    RECT r=spectrumBounds(dip(hwnd,client.right),std::max(pageHeight(),dip(hwnd,client.bottom)));
    RECT dirty{px(hwnd,r.left),px(hwnd,r.top-scrollY),px(hwnd,r.right),px(hwnd,r.bottom-scrollY)};
    ++app.visualInvalidations;
    InvalidateRect(hwnd,&dirty,FALSE);
}
void paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const RECT dirty = ps.rcPaint;
    const int width = dirty.right - dirty.left, height = dirty.bottom - dirty.top;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    HGDIOBJ previous = bitmap ? SelectObject(buffer, bitmap) : nullptr;
    const bool buffered = previous && previous != HGDI_ERROR;
    {
        Gdiplus::Graphics g(buffered ? buffer : dc);
        if (buffered) g.TranslateTransform(-static_cast<float>(dirty.left), -static_cast<float>(dirty.top));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        g.Clear(col(kBg));
        const float scale=static_cast<float>(GetDpiForWindow(hwnd))/96.f;
        g.SetPageUnit(Gdiplus::UnitPixel);
        g.ScaleTransform(scale,scale);
        g.TranslateTransform(0,-static_cast<float>(scrollY));
        const int cw=dip(hwnd,client.right), ch=std::max(pageHeight(),dip(hwnd,client.bottom));
        app.hits.clear();
        drawNav(g,ch);
        switch(app.page){
        case 0:drawHome(g,cw,ch);break;
        case 1:drawEq(g,cw,ch);break;
        case 2:drawLibrary(g,cw,ch);break;
        case 3:drawDevices(g,cw,ch);break;
        }
    }
    if (buffered) {
        BitBlt(dc, dirty.left, dirty.top, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (buffer) DeleteDC(buffer);
    ++app.paintCount;
#if defined(_DEBUG)
    const auto now=GetTickCount64();if(app.lastPaintLog==0)app.lastPaintLog=now;if(now-app.lastPaintLog>=60000){wchar_t line[192]{};swprintf_s(line,L"SonicSync UI paint stats: %llu paints, %llu full invalidations, %llu spectrum invalidations in 60s\n",static_cast<unsigned long long>(app.paintCount),static_cast<unsigned long long>(app.fullInvalidations),static_cast<unsigned long long>(app.visualInvalidations));OutputDebugStringW(line);app.paintCount=app.fullInvalidations=app.visualInvalidations=0;app.lastPaintLog=now;}
#endif
    EndPaint(hwnd,&ps);}
void setSlider(Action a,int x){const int left=kSidebar+40;RECT cr{};GetClientRect(app.window,&cr);const int cw=dip(app.window,cr.right)-left-34;const int width=(cw-44)/3,group=a==Action::SliderBass?0:(a==Action::SliderMid?1:2),sx=left+group*(width+22);const int val=std::clamp((x-sx)*24000/std::max(1,width)-12000,-12000,12000);app.eq.setTone(group,val);publish();invalidateFull(app.window);}
void refreshDevices() {
    if (app.process || app.routeProcess) {
        app.status = L"Stop playback or routing before refreshing devices";
        return;
    }
    const auto oldId = [](int i) -> std::wstring {
        return i >= 0 && i < static_cast<int>(app.endpoints.size()) ? app.endpoints[static_cast<std::size_t>(i)].id : L"";
    };
    const auto output = oldId(app.selectedEndpoint), source = oldId(app.routeSourceEndpoint);
    std::vector<Endpoint> devices;
    const bool found = enumerateEndpoints(devices);
    app.endpoints = std::move(devices);
    app.selectedEndpoint = app.endpoints.empty() ? -1 : 0;
    app.routeSourceEndpoint = -1;
    for (int i = 0; i < static_cast<int>(app.endpoints.size()); ++i) {
        if (app.endpoints[static_cast<std::size_t>(i)].isDefault) app.selectedEndpoint = i;
    }
    for (int i = 0; i < static_cast<int>(app.endpoints.size()); ++i) {
        const auto& id = app.endpoints[static_cast<std::size_t>(i)].id;
        if (id == output) app.selectedEndpoint = i;
        if (id == source) app.routeSourceEndpoint = i;
    }
    app.status = found ? L"Audio devices refreshed" : L"No active audio outputs found";
}

void activate(const Hit hit, int mouseX) {
    switch (hit.action) {
    case Action::Nav:
        app.page = hit.index;
        updateScroll(app.window,0);
        break;
    case Action::Add:
        addFiles();
        break;
    case Action::Play:
        if (app.process) {
            if (playbackPaused()) {
                SetEvent(app.playEvent);
                app.status = L"Playback resumed";
            } else {
                ResetEvent(app.playEvent);
                app.status = L"Playback paused";
            }
        }
        else if (app.library.songs.empty()) app.status = L"Add a WAV file first";
        else startPlayer(std::max(0, app.library.selected));
        break;
    case Action::Stop:
        stopPlayer();
        app.status = L"Stopped";
        break;
    case Action::Next: {
        const int next = app.library.next(true);
        if (next >= 0) {
            stopPlayer();
            startPlayer(next);
        }
        break;
    }
    case Action::Remove:
        if (hit.index == app.library.current) stopPlayer();
        app.library.remove(hit.index);
        app.status = L"Track removed from library";
        break;
    case Action::LibraryPage:
        app.library.page = std::clamp(app.library.page + hit.index, 0, app.library.lastPage(currentLibraryRows()));
        break;
    case Action::RefreshDevices:
        refreshDevices();
        break;
    case Action::Profile:
        app.eq.select(hit.index);
        publish();
        app.status = std::wstring(kPresetNames[hit.index]) + L" profile selected";
        break;
    case Action::Bypass:
        app.eq.bypass = !app.eq.bypass;
        publish();
        app.status = app.eq.bypass ? L"Bypass · original audio" : L"EQ processing enabled";
        break;
    case Action::Endpoint:
        if (app.process || app.routeProcess) app.status = L"Stop playback or routing before changing the output";
        else if (!app.endpoints.empty()) {
            app.selectedEndpoint = (app.selectedEndpoint + 1) % static_cast<int>(app.endpoints.size());
            app.status = L"Output selected · " + app.endpoints[static_cast<std::size_t>(app.selectedEndpoint)].name;
        }
        break;
    case Action::SourceEndpoint:
        if (app.routeProcess) app.status = L"Stop routing before changing the source";
        else if (!app.endpoints.empty()) {
            app.routeSourceEndpoint = (app.routeSourceEndpoint + 1) % static_cast<int>(app.endpoints.size());
            app.status = L"Route source · " + app.endpoints[static_cast<std::size_t>(app.routeSourceEndpoint)].name;
        }
        break;
    case Action::RouteToggle:
        if (app.routeProcess) {
            stopRoute();
            app.status = L"External route stopped";
        } else startRoute();
        break;
    case Action::Song:
        app.library.selected = hit.index;
        stopPlayer();
        startPlayer(hit.index);
        break;
    case Action::SliderBass:
    case Action::SliderMid:
    case Action::SliderTreble:
        app.dragSlider = static_cast<int>(hit.action);
        SetCapture(app.window);
        setSlider(hit.action, mouseX);
        break;
    }
    invalidateFull(app.window);
}

void finishPlayer() {
    DWORD exitCode{};
    const bool known = GetExitCodeProcess(app.process, &exitCode) != FALSE;
    const int next = app.library.next(false);
    stopPlayer();
    if (known && exitCode == 0) {
        if (next >= 0) startPlayer(next);
        else app.status = L"Playlist finished";
    } else if (known && exitCode == 2) {
        app.status = L"Could not play this file. Check the WAV format and output device.";
    } else {
        app.status = L"Playback failed. Check the output on the Devices page.";
    }
}

LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){switch(m){case WM_CREATE:{app.window=w;Gdiplus::GdiplusStartupInput in;Gdiplus::GdiplusStartup(&app.gdiplus,&in,nullptr);enumerateEndpoints(app.endpoints);for(std::size_t i=0;i<app.endpoints.size();++i)if(app.endpoints[i].isDefault)app.selectedEndpoint=static_cast<int>(i);restoreStudio();app.mediaStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);app.mediaRefresh=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(app.mediaStop&&app.mediaRefresh)app.mediaThread=std::thread(metadataThread);SetTimer(w,kTimer,kVisualRefreshMs,nullptr);return 0;}case WM_PAINT:paint(w);return 0;case WM_SIZE:updateScroll(w,scrollY);invalidateFull(w);return 0;
case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={px(w,740),px(w,360)};return 0;}
case WM_MOUSEWHEEL:updateScroll(w,scrollY-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*80);invalidateFull(w);return 0;
case WM_VSCROLL:{int pos=scrollY;switch(LOWORD(wp)){case SB_THUMBTRACK:case SB_THUMBPOSITION:pos=HIWORD(wp);break;case SB_LINEUP:pos-=30;break;case SB_LINEDOWN:pos+=30;break;case SB_PAGEUP:pos-=300;break;case SB_PAGEDOWN:pos+=300;break;}updateScroll(w,pos);invalidateFull(w);return 0;}case WM_ERASEBKGND:return 1;case WM_MEDIA_REFRESH:invalidateFull(w);return 0;case WM_LBUTTONDOWN:{const POINT p{dip(w,GET_X_LPARAM(lp)),dip(w,GET_Y_LPARAM(lp))+scrollY};for(auto it=app.hits.rbegin();it!=app.hits.rend();++it)if(PtInRect(&it->r,p)){activate(*it,p.x);return 0;}return 0;}case WM_MOUSEMOVE:if(app.dragSlider>=0&&(wp&MK_LBUTTON)){setSlider(static_cast<Action>(app.dragSlider),dip(w,GET_X_LPARAM(lp)));return 0;}break;case WM_LBUTTONUP:if(app.dragSlider>=0){app.dragSlider=-1;ReleaseCapture();return 0;}break;case WM_TIMER:{bool stateChanged=false;auto* live=app.controls?app.controls:app.routeControls;if(live){const LONG rate=sonicsync::win::load(&live->sampleRate);if(rate>=8000 && rate!=app.eqRate){publish();stateChanged=true;}}if(app.process&&WaitForSingleObject(app.process,0)==WAIT_OBJECT_0){finishPlayer();stateChanged=true;}if(app.routeProcess&&WaitForSingleObject(app.routeProcess,0)==WAIT_OBJECT_0){const LONG error=app.routeControls?sonicsync::win::load(&app.routeControls->routeError):0;stopRoute();if(error){wchar_t message[128]{};swprintf_s(message,L"External route error · check endpoints (0x%08lX)",static_cast<unsigned long>(error));app.status=message;}else app.status=L"External route stopped · check source/output devices and Windows routing";stateChanged=true;}const LONG routeState=app.routeControls?sonicsync::win::load(&app.routeControls->routeStatus):0;if(routeState!=app.lastRouteState){app.lastRouteState=routeState;stateChanged=true;}if(stateChanged)invalidateFull(w);else if(app.page==0&&(app.process||routeState==2)){auto*c=app.routeProcess?app.routeControls:app.controls;if(c){bool changed=!app.spectrumSnapshotValid;for(int i=0;i<16;++i){const LONG value=sonicsync::win::load(&c->spectrumMilli[i]);if(value!=app.lastSpectrum[i]){changed=true;app.lastSpectrum[i]=value;}}app.spectrumSnapshotValid=true;if(changed)invalidateSpectrum(w);}}return 0;}case WM_KEYDOWN:if(app.page==2&&(wp==VK_PRIOR||wp==VK_NEXT)){activate({{},Action::LibraryPage,wp==VK_NEXT?1:-1},0);return 0;}if(wp==VK_TAB){app.page=(app.page+1)%4;updateScroll(w,0);invalidateFull(w);return 0;}if(wp==VK_SPACE){if(!(lp&(1L<<30)))activate({{},Action::Play,0},0);return 0;}if(wp==VK_LEFT||wp==VK_RIGHT){app.eq.select((app.eq.profile+(wp==VK_RIGHT?1:9)+10)%10);publish();invalidateFull(w);return 0;}break;case WM_CLOSE:if(saveStudioBeforeClose(w))DestroyWindow(w);return 0;case WM_DESTROY:KillTimer(w,kTimer);stopPlayer();stopRoute();app.mediaStopping.store(true);if(app.mediaStop)SetEvent(app.mediaStop);if(app.mediaRefresh)SetEvent(app.mediaRefresh);if(app.mediaThread.joinable())app.mediaThread.join();if(app.mediaStop)CloseHandle(app.mediaStop);if(app.mediaRefresh)CloseHandle(app.mediaRefresh);if(app.gdiplus)Gdiplus::GdiplusShutdown(app.gdiplus);PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    int count{}; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(args){for(int i=1;i+1<count;++i) if(std::wstring_view(args[i])==L"--session") sessionOverride=fs::absolute(args[++i]);LocalFree(args);}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"SonicSync.Audio");
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HICON iconLarge=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_SONICSYNC),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR));
    HICON iconSmall=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_SONICSYNC),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.hIcon=iconLarge;wc.hIconSm=iconSmall;wc.hbrBackground=nullptr;wc.lpszClassName=L"SonicSyncStudio";
    if(!RegisterClassExW(&wc)){if(iconLarge)DestroyIcon(iconLarge);if(iconSmall)DestroyIcon(iconSmall);CoUninitialize();return 1;}
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"SonicSync",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,work.left,work.top,std::min<LONG>(work.right-work.left,MulDiv(1240,GetDpiForSystem(),96)),std::min<LONG>(work.bottom-work.top,MulDiv(820,GetDpiForSystem(),96)),nullptr,nullptr,instance,nullptr);
    if(!w){UnregisterClassW(wc.lpszClassName,instance);if(iconLarge)DestroyIcon(iconLarge);if(iconSmall)DestroyIcon(iconSmall);CoUninitialize();return 1;}
    SendMessageW(w,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(iconLarge));SendMessageW(w,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(iconSmall));
    ShowWindow(w,show);UpdateWindow(w);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DestroyIcon(iconLarge);DestroyIcon(iconSmall);UnregisterClassW(wc.lpszClassName,instance);CoUninitialize();return static_cast<int>(msg.wParam);
}

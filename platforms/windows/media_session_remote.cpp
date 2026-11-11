#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

#include <iostream>
#include <string>

using namespace winrt;
using namespace Windows::Media::Control;

static std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

static std::string json(const std::wstring& value) {
    const auto input = utf8(value);
    std::string output = "\"";
    for (const unsigned char c : input) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: if (c >= 0x20) output.push_back(static_cast<char>(c)); break;
        }
    }
    return output + '"';
}

static std::string json(const hstring& value) {
    return json(static_cast<std::wstring>(value));
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    try {
        init_apartment(apartment_type::multi_threaded);
        const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        const auto session = manager.GetCurrentSession();
        const std::wstring action = argv[1];
        if (action == L"--play" || action == L"--pause" || action == L"--toggle" || action == L"--next" || action == L"--previous") {
            if (!session) { std::cout << "{\"ok\":false,\"error\":\"no_media_session\"}\n"; return 0; }
            bool ok = false;
            if (action == L"--play") ok = session.TryPlayAsync().get();
            else if (action == L"--pause") ok = session.TryPauseAsync().get();
            else if (action == L"--toggle") ok = session.TryTogglePlayPauseAsync().get();
            else if (action == L"--next") ok = session.TrySkipNextAsync().get();
            else ok = session.TrySkipPreviousAsync().get();
            std::cout << (ok ? "{\"ok\":true}\n" : "{\"ok\":false,\"error\":\"command_not_supported\"}\n");
            return 0;
        }
        if (action != L"--state") return 2;
        if (!session) { std::cout << "{\"available\":false}\n"; return 0; }
        const auto properties = session.TryGetMediaPropertiesAsync().get();
        const auto status = session.GetPlaybackInfo().PlaybackStatus();
        const auto state = status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? L"playing" :
            status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ? L"paused" : L"stopped";
        std::cout << "{\"available\":true,\"title\":" << json(properties.Title())
                  << ",\"artist\":" << json(properties.Artist()) << ",\"album\":" << json(properties.AlbumTitle())
                  << ",\"source\":" << json(session.SourceAppUserModelId()) << ",\"playback\":" << json(std::wstring(state)) << "}\n";
        return 0;
    } catch (const hresult_error& error) {
        std::cerr << "Windows media session error: " << utf8(static_cast<std::wstring>(error.message())) << '\n';
        if (std::wstring(argv[1]) == L"--state") std::cout << "{\"available\":false,\"error\":\"session_service_unavailable\"}\n";
        else std::cout << "{\"ok\":false,\"error\":\"session_service_unavailable\"}\n";
        return 0;
    } catch (...) {
        std::cerr << "Windows media session is unavailable.\n";
        if (std::wstring(argv[1]) == L"--state") std::cout << "{\"available\":false,\"error\":\"session_service_unavailable\"}\n";
        else std::cout << "{\"ok\":false,\"error\":\"session_service_unavailable\"}\n";
        return 0;
    }
}

#include "studio_session.hpp"

#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace sonicsync::win {
namespace fs = std::filesystem;
namespace {
constexpr std::streamoff maxBytes = 8 * 1024 * 1024;
constexpr std::size_t maxTracks = 32768;
constexpr char signature[] = "SonicSync Studio 1";
bool validGain(int value) { return value >= -6000 && value <= 6000; }
}

StudioSession loadSession(const fs::path& file) {
    if (!fs::exists(file)) return {};
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const std::streamoff size = input.tellg();
    if (size < 0 || size > maxBytes) throw std::runtime_error("Invalid studio session size");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), size)) throw std::runtime_error("Cannot read studio session");

    std::istringstream lines(std::move(bytes));
    std::string line;
    if (!std::getline(lines, line) || (line != signature && line != "SonicSync Studio 2"))
        throw std::runtime_error("Unknown studio session");
    StudioSession session;
    if(line=="SonicSync Studio 2" && (!std::getline(lines,session.eq) || session.eq.empty()))
        throw std::runtime_error("Missing EQ state");
    if(!std::getline(lines,line)) throw std::runtime_error("Missing studio library");
    std::size_t count{};
    std::istringstream fields(line);
    fields.imbue(std::locale::classic());
    if (!(fields >> session.profile >> session.bass >> session.mid >> session.treble >> session.bypass >> session.selected >> count)
        || (fields >> line) || count > maxTracks || session.selected < -1 || session.selected >= static_cast<int>(count)
        || !validGain(session.bass) || !validGain(session.mid) || !validGain(session.treble))
        throw std::runtime_error("Invalid studio settings");
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::getline(lines, line) || line.empty() || line.find('\0') != std::string::npos)
            throw std::runtime_error("Incomplete studio library");
        fs::path path{std::u8string(line.begin(), line.end())};
        if (!path.is_absolute()) throw std::runtime_error("Invalid studio library path");
        session.songs.push_back(std::move(path));
    }
    if (std::getline(lines, line)) throw std::runtime_error("Unexpected studio session data");
    return session;
}

void saveSession(const fs::path& file, const StudioSession& session) {
    if (session.songs.size() > maxTracks) throw std::runtime_error("Studio library is too large");
    fs::create_directories(file.parent_path());
    auto temporary = file;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.imbue(std::locale::classic());
        if(session.eq.empty()) output << signature << '\n';
        else output << "SonicSync Studio 2\n" << session.eq << '\n';
        output << session.profile << ' ' << session.bass << ' ' << session.mid << ' '
               << session.treble << ' ' << session.bypass << ' ' << session.selected << ' ' << session.songs.size() << '\n';
        for (const auto& path : session.songs) {
            const auto bytes = path.u8string();
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.put('\n');
        }
        if (output.tellp() > maxBytes) throw std::runtime_error("Studio library is too large");
        output.close();
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace studio session");
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
} // namespace sonicsync::win

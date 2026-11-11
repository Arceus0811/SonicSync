#include "studio_session.hpp"

#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace sonicsync::win;

int main() {
    const auto folder = fs::temp_directory_path() / (L"SonicSync-session-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto file = folder / "studio-session.txt";
    try {
        if (loadSession(file) != StudioSession{}) throw std::runtime_error("First launch should start empty");
        StudioSession first{"hip-hop", 1200, -500, 400, true, 1, {folder / L"first.wav", folder / L"演奏 02.wav"}};
        saveSession(file, first);
        if (loadSession(file) != first) throw std::runtime_error("Unicode library or EQ changed on restart");
        first.songs.clear();
        first.selected = -1;
        saveSession(file, first);
        if (loadSession(file) != first) throw std::runtime_error("Removed tracks returned on restart");
        std::ofstream(file) << "SonicSync Studio 1\nneutral 0 0 0 0 0 2\n";
        bool rejected = false;
        try { loadSession(file); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("A truncated library was restored");
        fs::remove(file);
        fs::remove(folder);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sonicsync::win {

struct StudioSession {
    std::string profile{"neutral"};
    int bass{}, mid{}, treble{};
    bool bypass{};
    int selected{-1};
    std::vector<std::filesystem::path> songs;
    std::string eq;
    bool operator==(const StudioSession&) const = default;
};

// An absent file is a new session. Read/write failures throw so the UI can
// distinguish a damaged session from a first launch.
StudioSession loadSession(const std::filesystem::path& file);
void saveSession(const std::filesystem::path& file, const StudioSession& session);

} // namespace sonicsync::win

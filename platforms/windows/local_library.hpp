#pragma once

#include <algorithm>
#include <filesystem>
#include <vector>

namespace sonicsync::win {

struct LocalLibrary {
    std::vector<std::filesystem::path> songs;
    int selected{-1};
    int current{-1};
    int page{};

    int count() const { return static_cast<int>(songs.size()); }

    bool add(const std::filesystem::path& path) {
        const auto it = std::find(songs.begin(), songs.end(), path);
        if (it != songs.end()) {
            selected = static_cast<int>(it - songs.begin());
            return false;
        }
        selected = count();
        songs.push_back(path);
        return true;
    }

    void remove(int index) {
        if (index < 0 || index >= count()) return;
        songs.erase(songs.begin() + index);
        if (current == index) current = -1;
        else if (current > index) --current;
        if (selected > index) --selected;
        else if (selected == index) selected = std::min(index, count() - 1);
    }

    // Manual Next wraps. Natural completion stops at the end of the list.
    int next(bool wrap) const {
        if (current >= 0 && current + 1 < count()) return current + 1;
        return wrap && !songs.empty() ? 0 : -1;
    }

    int lastPage(int rows) const { return songs.empty() ? 0 : (count() - 1) / rows; }
};

} // namespace sonicsync::win

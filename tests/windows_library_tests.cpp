#include "../platforms/windows/local_library.hpp"

#include <iostream>

int main() {
    sonicsync::win::LocalLibrary library;
    if (library.next(true) != -1 || library.lastPage(2) != 0) return 1;
    library.add("a.wav");
    library.add("b.wav");
    library.add("c.wav");
    if (library.add("b.wav") || library.count() != 3 || library.selected != 1 || library.lastPage(2) != 1)
        return 1;
    library.current = 1;
    library.remove(0);
    if (library.current != 0 || library.selected != 0 || library.songs[0] != "b.wav" || library.next(false) != 1)
        return 1;
    library.current = 1;
    if (library.next(false) != -1 || library.next(true) != 0) return 1;
    library.remove(1);
    if (library.current != -1 || library.selected != 0 || library.lastPage(2) != 0) return 1;
    library.remove(0);
    if (library.current != -1 || library.selected != -1 || library.next(true) != -1) return 1;
    std::cout << "Library import, paging, selection, removal and queue boundaries passed\n";
    return 0;
}

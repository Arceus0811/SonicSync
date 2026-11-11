#include "artwork.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <windows.h>

int main() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { std::cerr << "could not initialize COM for WIC test\n"; return 1; }
    std::ifstream input(SONICSYNC_TEST_IMAGE_PATH, std::ios::binary);
    if (!input) { std::cerr << "could not open bundled PNG test image\n"; CoUninitialize(); return 1; }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    const auto image = sonicsync::windows::decodeArtwork(bytes.data(), bytes.size());
    if (!image || image->width != 512 || image->height != 512 || image->bgra.size() != 512U * 512U * 4U) {
        std::cerr << "valid PNG failed to decode and resize\n"; CoUninitialize(); return 1;
    }
    const std::uint8_t invalid[]{0, 1, 2, 3, 4, 5};
    if (sonicsync::windows::decodeArtwork(invalid, sizeof(invalid))) {
        std::cerr << "unsupported bytes should return an empty image\n"; CoUninitialize(); return 1;
    }
    if (sonicsync::windows::decodeArtwork(nullptr, 0)) {
        std::cerr << "empty artwork should return an empty image\n"; CoUninitialize(); return 1;
    }
    std::cout << "Windows artwork decode, resize, invalid-data, and empty-data checks passed\n";
    CoUninitialize();
    return 0;
}

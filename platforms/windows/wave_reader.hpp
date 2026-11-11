#pragma once

#include <windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <istream>
#include <string>
#include <vector>

namespace sonicsync::win {

struct WaveFile {
    WAVEFORMATEXTENSIBLE format{};
    std::vector<BYTE> audio;
};

inline bool readWave(std::istream& stream, WaveFile& wave, std::wstring& error) {
    const auto u16 = [](const BYTE* p) {
        return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
    };
    const auto u32 = [](const BYTE* p) {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    };
    const auto fail = [&](const wchar_t* reason) { error = reason; return false; };
    if (!stream) return fail(L"Could not open the WAV file.");
    stream.seekg(0, std::ios::end);
    const std::streamoff fileSize = stream.tellg();
    if (fileSize < 12 || fileSize > 512LL * 1024 * 1024)
        return fail(L"WAV must be at least 12 bytes and no larger than 512 MiB.");
    stream.seekg(0);
    std::array<BYTE,12> header{};
    if (!stream.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4))
        return fail(L"Input must be a RIFF/WAVE file.");
    const std::int64_t riffEnd = static_cast<std::int64_t>(u32(header.data()+4)) + 8;
    if (riffEnd > fileSize || riffEnd < 12) return fail(L"Invalid RIFF length.");

    std::array<BYTE,40> fmt{};
    std::uint32_t fmtSize = 0, dataSize = 0;
    std::int64_t dataOffset = 0;
    for (std::int64_t pos = 12; pos < riffEnd;) {
        std::array<BYTE,8> chunk{};
        stream.seekg(pos);
        if (riffEnd - pos < 8 || !stream.read(reinterpret_cast<char*>(chunk.data()), chunk.size()))
            return fail(L"Truncated WAV chunk header.");
        const auto size = u32(chunk.data() + 4);
        const auto start = pos + 8;
        if (size > riffEnd - start) return fail(L"WAV chunk exceeds the file bounds.");
        if (!std::memcmp(chunk.data(), "fmt ", 4)) {
            fmtSize = size;
            const auto copy = std::min<std::size_t>(fmt.size(), size);
            if (!stream.read(reinterpret_cast<char*>(fmt.data()), static_cast<std::streamsize>(copy)))
                return fail(L"Could not read the WAV format chunk.");
        } else if (!std::memcmp(chunk.data(), "data", 4)) {
            dataOffset = start;
            dataSize = size;
        }
        pos = start + size + (size & 1U);
    }
    if (fmtSize < 16 || dataSize == 0) return fail(L"WAV is missing a valid format or audio data chunk.");

    WAVEFORMATEXTENSIBLE format{};
    auto& f = format.Format;
    f.wFormatTag = u16(fmt.data());
    f.nChannels = u16(fmt.data() + 2);
    f.nSamplesPerSec = u32(fmt.data() + 4);
    f.nAvgBytesPerSec = u32(fmt.data() + 8);
    f.nBlockAlign = u16(fmt.data() + 12);
    f.wBitsPerSample = u16(fmt.data() + 14);
    const unsigned extension = fmtSize >= 18 ? u16(fmt.data() + 16) : 0U;
    if (fmtSize == 17 || (fmtSize >= 18 && extension > fmtSize - 18))
        return fail(L"WAV format extension exceeds its format chunk.");
    if (f.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (extension < 22) return fail(L"WAVE_FORMAT_EXTENSIBLE header is truncated.");
        format.Samples.wValidBitsPerSample = u16(fmt.data() + 18);
        format.dwChannelMask = u32(fmt.data() + 20);
        std::memcpy(&format.SubFormat, fmt.data() + 24, sizeof(GUID));
        // WASAPI may read cbSize bytes after WAVEFORMATEX. Only these 22 bytes are retained.
        f.cbSize = 22;
        if (format.Samples.wValidBitsPerSample != f.wBitsPerSample)
            return fail(L"Extensible WAV files must use all bits in each sample container.");
    }
    if ((f.nChannels < 1 || f.nChannels > 16) || f.nSamplesPerSec < 8000 || f.nSamplesPerSec > 384000 ||
        f.nBlockAlign == 0 || dataSize % f.nBlockAlign != 0 || f.wBitsPerSample % 8 != 0 ||
        f.nBlockAlign != f.nChannels * (f.wBitsPerSample / 8) ||
        f.nAvgBytesPerSec != f.nSamplesPerSec * f.nBlockAlign)
        return fail(L"Only valid PCM or float WAV files with 1 to 16 channels at 8 kHz to 384 kHz are supported.");
    const bool pcm = f.wFormatTag == WAVE_FORMAT_PCM ||
        (f.wFormatTag == WAVE_FORMAT_EXTENSIBLE && IsEqualGUID(format.SubFormat, KSDATAFORMAT_SUBTYPE_PCM));
    const bool flt = f.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (f.wFormatTag == WAVE_FORMAT_EXTENSIBLE && IsEqualGUID(format.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
    if ((!pcm && !flt) || (flt && f.wBitsPerSample != 32) ||
        (pcm && f.wBitsPerSample != 8 && f.wBitsPerSample != 16 && f.wBitsPerSample != 24 && f.wBitsPerSample != 32))
        return fail(L"Supported samples are PCM 8/16/24/32-bit or IEEE float32.");

    wave.audio.resize(dataSize);
    stream.seekg(dataOffset);
    if (!stream.read(reinterpret_cast<char*>(wave.audio.data()), dataSize))
        return fail(L"Could not read the WAV audio data.");
    wave.format = format;
    return true;
}

} // namespace sonicsync::win

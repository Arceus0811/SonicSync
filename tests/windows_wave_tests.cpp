#define NOMINMAX
#include "../platforms/windows/wave_reader.hpp"

#include <iostream>
#include <sstream>

namespace {
void append16(std::string& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value));
    bytes.push_back(static_cast<char>(value >> 8));
}

void append32(std::string& bytes, std::uint32_t value) {
    append16(bytes, static_cast<std::uint16_t>(value));
    append16(bytes, static_cast<std::uint16_t>(value >> 16));
}

std::string chunk(const char* id, const std::string& payload) {
    std::string bytes = id;
    append32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes += payload;
    if (payload.size() % 2) bytes.push_back(0);
    return bytes;
}

std::string format(bool extensible) {
    std::string fmt;
    append16(fmt, extensible ? WAVE_FORMAT_EXTENSIBLE : WAVE_FORMAT_PCM);
    append16(fmt, 1);
    append32(fmt, 44100);
    append32(fmt, 88200);
    append16(fmt, 2);
    append16(fmt, 16);
    // Include vendor bytes after the known format fields.
    append16(fmt, extensible ? 28 : 6);
    if (extensible) {
        append16(fmt, 16);
        append32(fmt, SPEAKER_FRONT_CENTER);
        const GUID pcm = KSDATAFORMAT_SUBTYPE_PCM;
        fmt.append(reinterpret_cast<const char*>(&pcm), sizeof(pcm));
    }
    fmt.append(6, 'v');
    return fmt;
}

std::string riff(const std::string& fmt, const std::string& audio) {
    const auto body = "WAVE" + chunk("JUNK", "x") + chunk("data", audio) + chunk("fmt ", fmt);
    std::string bytes = "RIFF";
    append32(bytes, static_cast<std::uint32_t>(body.size()));
    return bytes + body;
}

bool parse(const std::string& bytes, sonicsync::win::WaveFile& wave) {
    std::istringstream stream(bytes, std::ios::binary);
    std::wstring error;
    return sonicsync::win::readWave(stream, wave, error);
}
} // namespace

int main() {
    sonicsync::win::WaveFile wave;
    const std::string audio("\x23\x01\x34\x02", 4);
    for (bool ext : {false, true}) {
        const auto bytes = riff(format(ext), audio);
        if (!parse(bytes, wave) || wave.format.Format.cbSize != (ext ? 22 : 0) ||
            wave.audio.size() != audio.size() || std::memcmp(wave.audio.data(), audio.data(), audio.size())) {
            std::cerr << "Valid WAV with extra format bytes should expose only its initialized format\n";
            return 1;
        }
        if (parse(bytes.substr(0, bytes.size() - 1), wave)) return 1;
    }
    if (parse(riff(format(true).substr(0, 39), audio), wave)) return 1;
    if (parse(riff(format(false), "x"), wave)) return 1;
    auto badRate = format(false);
    badRate[8] = 0;
    if (parse(riff(badRate, audio), wave)) return 1;
    std::cout << "PCM and extensible WAV parsing, chunk bounds and client format size passed\n";
    return 0;
}

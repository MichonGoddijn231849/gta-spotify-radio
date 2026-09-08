#include "silent_track.h"

#include "log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::filesystem::path documents_directory() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw))) {
        return {};
    }
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

void append_id3_text_frame(std::vector<uint8_t>& tag, const char (&id)[5],
                           const std::string& text) {
    const uint32_t size = static_cast<uint32_t>(text.size()) + 1;
    for (int i = 0; i < 4; ++i) tag.push_back(static_cast<uint8_t>(id[i]));
    tag.push_back(static_cast<uint8_t>(size >> 24));
    tag.push_back(static_cast<uint8_t>(size >> 16));
    tag.push_back(static_cast<uint8_t>(size >> 8));
    tag.push_back(static_cast<uint8_t>(size));
    tag.push_back(0);
    tag.push_back(0);
    tag.push_back(0); // ISO-8859-1
    tag.insert(tag.end(), text.begin(), text.end());
}

std::vector<uint8_t> build_id3_tag() {
    std::vector<uint8_t> frames;
    append_id3_text_frame(frames, "TIT2", "Spotify");
    append_id3_text_frame(frames, "TPE1", "GTA V Radio");
    append_id3_text_frame(frames, "TALB", "GTA Spotify Radio");

    const uint32_t size = static_cast<uint32_t>(frames.size());
    std::vector<uint8_t> tag{uint8_t{'I'}, uint8_t{'D'}, uint8_t{'3'},
                             uint8_t{3}, uint8_t{0}, uint8_t{0}};
    // ID3v2 sizes are syncsafe: seven bits per byte.
    tag.push_back(static_cast<uint8_t>((size >> 21) & 0x7F));
    tag.push_back(static_cast<uint8_t>((size >> 14) & 0x7F));
    tag.push_back(static_cast<uint8_t>((size >> 7) & 0x7F));
    tag.push_back(static_cast<uint8_t>(size & 0x7F));
    tag.insert(tag.end(), frames.begin(), frames.end());
    return tag;
}

} // namespace

namespace gsr {

std::filesystem::path find_user_music_dir(const std::wstring& override_dir) {
    std::error_code ec;
    if (!override_dir.empty()) {
        std::filesystem::path path(override_dir);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    const std::filesystem::path documents = documents_directory();
    if (documents.empty()) return {};

    const std::filesystem::path rockstar = documents / "Rockstar Games";
    const wchar_t* game_folders[] = {L"GTA V Enhanced", L"GTA V"};
    for (const wchar_t* folder : game_folders) {
        const std::filesystem::path game = rockstar / folder;
        if (!std::filesystem::is_directory(game, ec)) continue;
        const std::filesystem::path music = game / "User Music";
        if (!std::filesystem::is_directory(music, ec)) {
            std::filesystem::create_directories(music, ec);
        }
        if (std::filesystem::is_directory(music, ec)) return music;
    }
    return {};
}

SilentTrackResult ensure_silent_track(const std::filesystem::path& user_music_dir,
                                      int minutes) {
    SilentTrackResult result;
    if (user_music_dir.empty()) {
        result.detail = "no GTA user music folder found";
        return result;
    }

    result.path = user_music_dir / "00 GTA Spotify Radio (silence).mp3";

    std::error_code ec;
    if (std::filesystem::exists(result.path, ec)) {
        result.ok = true;
        result.detail = "placeholder already present";
        return result;
    }

    // MPEG-1 Layer III, 44100 Hz, 64 kbit/s, mono. An all-zero side info
    // block means zero spectral coefficients, which every decoder renders as
    // digital silence.
    constexpr uint8_t kHeader[4] = {0xFF, 0xFB, 0x50, 0xC4};
    constexpr size_t kFrameBytes = 208;
    constexpr double kFramesPerSecond = 44100.0 / 1152.0;

    const size_t frame_count =
        static_cast<size_t>(kFramesPerSecond * 60.0 * static_cast<double>(minutes));

    std::vector<uint8_t> frame(kFrameBytes, 0);
    std::memcpy(frame.data(), kHeader, sizeof(kHeader));

    std::ofstream out(result.path, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.detail = "could not write to the user music folder";
        return result;
    }

    const std::vector<uint8_t> tag = build_id3_tag();
    out.write(reinterpret_cast<const char*>(tag.data()),
              static_cast<std::streamsize>(tag.size()));
    for (size_t i = 0; i < frame_count && out; ++i) {
        out.write(reinterpret_cast<const char*>(frame.data()),
                  static_cast<std::streamsize>(frame.size()));
    }
    out.close();

    if (!std::filesystem::exists(result.path, ec)) {
        result.detail = "writing the placeholder track failed";
        return result;
    }

    result.ok = true;
    result.created = true;
    result.detail = "wrote " + std::to_string(minutes) + " minute placeholder";
    log::info("Created Self Radio placeholder: " + result.path.string());
    return result;
}

} // namespace gsr

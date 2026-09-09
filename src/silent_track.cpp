#include "silent_track.h"

#include "log.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <vector>

namespace {

std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

// Documents can be redirected (OneDrive, a moved user folder), and the game
// does not always end up under the same root the shell reports, so collect
// every plausible one instead of trusting a single answer.
std::vector<std::filesystem::path> documents_roots() {
    std::vector<std::filesystem::path> roots;
    const auto add = [&roots](std::filesystem::path path) {
        if (path.empty()) return;
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) return;
        for (const auto& existing : roots) {
            if (existing == path) return;
        }
        roots.push_back(std::move(path));
    };

    add(known_folder(FOLDERID_Documents));

    const std::filesystem::path profile = known_folder(FOLDERID_Profile);
    if (!profile.empty()) {
        add(profile / "Documents");
        add(profile / "OneDrive" / "Documents");
    }

    wchar_t onedrive[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"OneDrive", onedrive, MAX_PATH) > 0) {
        add(std::filesystem::path(onedrive) / "Documents");
    }
    return roots;
}

std::wstring to_lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return value;
}

// Higher is a better guess at "this is the GTA install the player is running".
// Rockstar spells the Enhanced folder "GTAV Enhanced" but the Legacy one
// "GTA V", so compare with spaces stripped rather than matching literals.
int score_game_folder(const std::filesystem::path& folder) {
    std::wstring name = to_lower(folder.filename().wstring());
    name.erase(std::remove(name.begin(), name.end(), L' '), name.end());

    if (name.find(L"gta") == std::wstring::npos &&
        name.find(L"grandtheftauto") == std::wstring::npos) {
        return 0;
    }
    int score = 1;
    if (name.find(L"enhanced") != std::wstring::npos) score = 3;
    else if (name == L"gtav" || name == L"grandtheftautov") score = 2;

    // The name decides which game this is; an existing User Music folder only
    // breaks ties within a tier. Otherwise leftover Legacy user music would
    // outrank the Enhanced install this plugin actually runs in.
    std::error_code ec;
    const bool has_music = std::filesystem::is_directory(folder / "User Music", ec);
    return score * 10 + (has_music ? 1 : 0);
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

std::vector<uint8_t> build_id3_tag(uint64_t duration_ms) {
    std::vector<uint8_t> frames;
    append_id3_text_frame(frames, "TIT2", "Spotify");
    append_id3_text_frame(frames, "TPE1", "GTA V Radio");
    append_id3_text_frame(frames, "TALB", "GTA Spotify Radio");
    // Scanners that trust TLEN rather than parsing every frame still get a
    // sensible duration out of the file.
    append_id3_text_frame(frames, "TLEN", std::to_string(duration_ms));

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
        log::info("Using UserMusicDir override: " + path.string());
        return path;
    }

    std::filesystem::path best;
    int best_score = 0;

    const std::vector<std::filesystem::path> roots = documents_roots();
    if (roots.empty()) log::error("No Documents folder could be resolved");

    for (const std::filesystem::path& root : roots) {
        const std::filesystem::path rockstar = root / "Rockstar Games";
        if (!std::filesystem::is_directory(rockstar, ec)) {
            log::info("Probed (no Rockstar Games folder): " + root.string());
            continue;
        }
        log::info("Probing " + rockstar.string());

        std::filesystem::directory_iterator it(rockstar, ec), end;
        if (ec) {
            log::error("Could not list " + rockstar.string() + ": " + ec.message());
            continue;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_directory(ec)) continue;
            const int score = score_game_folder(it->path());
            log::info("  " + it->path().filename().string() +
                      " (score " + std::to_string(score) + ")");
            if (score > best_score) {
                best_score = score;
                best = it->path();
            }
        }
    }

    if (best.empty()) return {};

    log::info("Selected GTA folder: " + best.string());
    const std::filesystem::path music = best / "User Music";
    if (!std::filesystem::is_directory(music, ec)) {
        std::filesystem::create_directories(music, ec);
    }
    if (!std::filesystem::is_directory(music, ec)) {
        log::error("Could not create " + music.string());
        return {};
    }
    return music;
}

SilentTrackResult ensure_silent_track(const std::filesystem::path& user_music_dir,
                                      int minutes) {
    SilentTrackResult result;
    if (user_music_dir.empty()) {
        result.detail = "no GTA user music folder found - set UserMusicDir in "
                        "GtaSpotifyRadio.ini";
        return result;
    }

    result.path = user_music_dir / "00 GTA Spotify Radio (silence).mp3";

    // MPEG-1 Layer III, 48000 Hz, 64 kbit/s, mono. An all-zero side info
    // block means zero spectral coefficients, which every decoder renders as
    // digital silence. 48 kHz is deliberate: 144 * 64000 / 48000 is exactly
    // 192, so every frame is the same size and the stream never needs the
    // padding bit. At 44.1 kHz frames would be 208.98 bytes, and a fixed 208
    // leaves the file slightly under its nominal bitrate, which makes any
    // scanner that estimates duration from size disagree with the tag.
    constexpr uint8_t kHeader[4] = {0xFF, 0xFB, 0x54, 0xC4};
    constexpr size_t kFrameBytes = 192;
    constexpr size_t kSamplesPerFrame = 1152;
    constexpr size_t kSampleRate = 48000;

    const size_t frame_count = static_cast<size_t>(minutes) * 60 * kSampleRate /
                               kSamplesPerFrame;
    const uint64_t duration_ms =
        static_cast<uint64_t>(frame_count) * kSamplesPerFrame * 1000 / kSampleRate;
    const std::vector<uint8_t> tag = build_id3_tag(duration_ms);
    const uintmax_t expected_bytes = tag.size() + frame_count * kFrameBytes;

    std::error_code ec;
    if (std::filesystem::exists(result.path, ec)) {
        // Only trust a file that is exactly what we would have written. A
        // truncated one from an interrupted run would otherwise be kept
        // forever, and the game would never index it.
        const uintmax_t actual = std::filesystem::file_size(result.path, ec);
        if (!ec && actual == expected_bytes) {
            result.ok = true;
            result.detail = "placeholder already present";
            return result;
        }
        log::info("Rewriting placeholder: expected " + std::to_string(expected_bytes) +
                  " bytes, found " + std::to_string(actual));
    }

    std::vector<uint8_t> frame(kFrameBytes, 0);
    std::memcpy(frame.data(), kHeader, sizeof(kHeader));

    // Build under a temporary name and move it into place, so a failure part
    // way through never leaves a half-written track for the game to find.
    const std::filesystem::path temp = result.path.string() + ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.detail = "could not write to " + user_music_dir.string();
            return result;
        }
        out.write(reinterpret_cast<const char*>(tag.data()),
                  static_cast<std::streamsize>(tag.size()));
        for (size_t i = 0; i < frame_count && out; ++i) {
            out.write(reinterpret_cast<const char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size()));
        }
        if (!out) {
            out.close();
            std::filesystem::remove(temp, ec);
            result.detail = "ran out of space writing the placeholder track";
            return result;
        }
    }

    const uintmax_t written = std::filesystem::file_size(temp, ec);
    if (ec || written != expected_bytes) {
        std::filesystem::remove(temp, ec);
        result.detail = "placeholder track came out the wrong size";
        return result;
    }

    std::filesystem::rename(temp, result.path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        result.detail = "could not replace the placeholder track: " + ec.message();
        return result;
    }

    result.ok = true;
    result.created = true;
    result.detail = "wrote " + std::to_string(minutes) + " minute placeholder (" +
                    std::to_string(expected_bytes) + " bytes)";
    log::info("Created Self Radio placeholder: " + result.path.string());
    return result;
}

} // namespace gsr

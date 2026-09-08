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
int score_game_folder(const std::filesystem::path& folder) {
    const std::wstring name = to_lower(folder.filename().wstring());
    int score = 0;
    if (name.find(L"gta") != std::wstring::npos ||
        name.find(L"grand theft auto") != std::wstring::npos) {
        score = 1;
    }
    if (score == 0) return 0;
    if (name.find(L"enhanced") != std::wstring::npos) score = 3;
    else if (name == L"gta v" || name == L"grand theft auto v") score = 2;

    // An existing User Music folder is the strongest signal there is.
    std::error_code ec;
    if (std::filesystem::is_directory(folder / "User Music", ec)) score += 10;
    return score;
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

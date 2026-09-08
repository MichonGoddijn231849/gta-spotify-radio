#include "settings.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace {

std::string narrow_ascii(const std::wstring& input) {
    std::string out;
    out.reserve(input.size());
    for (const wchar_t c : input) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::wstring read_string(const std::filesystem::path& ini, const wchar_t* section,
                         const wchar_t* key, const std::wstring& fallback) {
    wchar_t buffer[512]{};
    GetPrivateProfileStringW(section, key, fallback.c_str(), buffer,
                             static_cast<DWORD>(std::size(buffer)), ini.c_str());
    return buffer;
}

int read_int(const std::filesystem::path& ini, const wchar_t* section,
             const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(section, key, fallback, ini.c_str());
}

bool read_bool(const std::filesystem::path& ini, const wchar_t* section,
               const wchar_t* key, bool fallback) {
    return read_int(ini, section, key, fallback ? 1 : 0) != 0;
}

float read_float(const std::filesystem::path& ini, const wchar_t* section,
                 const wchar_t* key, float fallback) {
    wchar_t fallback_text[32]{};
    swprintf(fallback_text, std::size(fallback_text), L"%g",
             static_cast<double>(fallback));
    const std::wstring text = read_string(ini, section, key, fallback_text);
    wchar_t* end = nullptr;
    const float value = std::wcstof(text.c_str(), &end);
    if (end == text.c_str()) return fallback;
    return value;
}

} // namespace

namespace gsr {

float db_to_linear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

Settings Settings::load(const std::filesystem::path& ini_path) {
    Settings s;

    const std::wstring mode = read_string(ini_path, L"Radio", L"Mode", L"Replace");
    if (_wcsicmp(mode.c_str(), L"SelfRadio") == 0) {
        s.mode = StationMode::SelfRadio;
    } else {
        s.mode = StationMode::Replace;
        if (_wcsicmp(mode.c_str(), L"Replace") != 0) {
            log::error("Unknown Radio/Mode '" + narrow_ascii(mode) +
                       "', falling back to Replace");
        }
    }

    const wchar_t* default_station =
        s.mode == StationMode::SelfRadio ? L"RADIO_19_USER" : L"RADIO_02_POP";
    s.station = narrow_ascii(read_string(ini_path, L"Radio", L"Station", default_station));
    s.fallback_station = narrow_ascii(
        read_string(ini_path, L"Radio", L"FallbackStation", L"RADIO_01_CLASS_ROCK"));
    s.unlock_station = read_bool(ini_path, L"Radio", L"UnlockStation", s.unlock_station);
    s.mobile_radio = read_bool(ini_path, L"Radio", L"MobileRadio", s.mobile_radio);
    s.mute_strategies = narrow_ascii(
        read_string(ini_path, L"Radio", L"MuteStrategies", L"Scene,Freeze"));
    s.mute_scenes = narrow_ascii(
        read_string(ini_path, L"Radio", L"MuteScenes",
                    L"MP_JOB_CHANGE_RADIO_MUTE,FBI_HEIST_H5_MUTE_RADIO_SCENE,"
                    L"CHARACTER_CHANGE_IN_SKY_SCENE"));

    s.generate_silent_track =
        read_bool(ini_path, L"SelfRadio", L"GenerateSilentTrack", s.generate_silent_track);
    s.silent_track_minutes = std::clamp(
        read_int(ini_path, L"SelfRadio", L"SilentTrackMinutes", s.silent_track_minutes),
        1, 120);
    s.user_music_dir = read_string(ini_path, L"SelfRadio", L"UserMusicDir", L"");

    s.volume_db = std::clamp(read_float(ini_path, L"Audio", L"VolumeDb", s.volume_db),
                             -60.0f, 12.0f);
    s.follow_music_slider =
        read_bool(ini_path, L"Audio", L"FollowMusicSlider", s.follow_music_slider);
    s.slider_curve = std::clamp(
        read_float(ini_path, L"Audio", L"SliderCurve", s.slider_curve), 0.5f, 4.0f);
    s.music_slider_override =
        std::clamp(read_int(ini_path, L"Audio", L"MusicSliderOverride", -1), -1, 10);
    s.spotify_volume =
        std::clamp(read_int(ini_path, L"Audio", L"SpotifyVolume", s.spotify_volume), 0, 65535);
    s.replaygain = read_bool(ini_path, L"Audio", L"ReplayGain", s.replaygain);
    s.buffer_ms = std::clamp(read_int(ini_path, L"Audio", L"BufferMs", s.buffer_ms), 120, 4000);

    s.duck_on_dialogue = read_bool(ini_path, L"Mix", L"DuckOnDialogue", s.duck_on_dialogue);
    s.dialogue_duck_db = std::clamp(
        read_float(ini_path, L"Mix", L"DialogueDuckDb", s.dialogue_duck_db), -60.0f, 0.0f);
    s.exterior_gain_db = std::clamp(
        read_float(ini_path, L"Mix", L"ExteriorGainDb", s.exterior_gain_db), -60.0f, 0.0f);
    s.exterior_range_m = std::clamp(
        read_float(ini_path, L"Mix", L"ExteriorRangeMeters", s.exterior_range_m), 1.0f, 80.0f);
    s.respect_game_radio_fade =
        read_bool(ini_path, L"Mix", L"RespectGameRadioFade", s.respect_game_radio_fade);
    s.cabin_filter = read_bool(ini_path, L"Mix", L"CabinFilter", s.cabin_filter);
    s.interior_cutoff_hz = std::clamp(
        read_float(ini_path, L"Mix", L"InteriorCutoffHz", s.interior_cutoff_hz),
        200.0f, 22050.0f);
    s.exterior_cutoff_hz = std::clamp(
        read_float(ini_path, L"Mix", L"ExteriorCutoffHz", s.exterior_cutoff_hz),
        200.0f, 22050.0f);
    s.submerged_cutoff_hz = std::clamp(
        read_float(ini_path, L"Mix", L"SubmergedCutoffHz", s.submerged_cutoff_hz),
        100.0f, 22050.0f);
    s.fade_ms = std::clamp(read_int(ini_path, L"Mix", L"FadeMs", s.fade_ms), 5, 2000);
    s.stream_pause_delay_ms = std::clamp(
        read_int(ini_path, L"Mix", L"StreamPauseDelayMs", s.stream_pause_delay_ms), 0, 30000);

    s.key_toggle = read_int(ini_path, L"Controls", L"SelectStation", s.key_toggle);
    s.key_play_pause = read_int(ini_path, L"Controls", L"PlayPause", s.key_play_pause);
    s.key_next = read_int(ini_path, L"Controls", L"Next", s.key_next);
    s.key_previous = read_int(ini_path, L"Controls", L"Previous", s.key_previous);

    log::info("Station mode: " +
              std::string(s.mode == StationMode::Replace ? "Replace" : "SelfRadio") +
              ", station " + s.station + ", mute " + s.mute_strategies);
    return s;
}

} // namespace gsr

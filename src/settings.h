#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace gsr {

// How the plugin claims a slot on the radio wheel.
enum class StationMode {
    // Take over a normal station (for example RADIO_02_POP). The native
    // station is silenced while the plugin is tuned in and restored when the
    // player retunes away.
    Replace,
    // Ride on Self Radio (RADIO_19_USER) with a silent placeholder track.
    // This needs the game to index user music, which GTA V Enhanced does not
    // do -- Self Radio stays off the wheel even with real MP3s present.
    SelfRadio,
};

struct Settings {
    // [Radio]
    StationMode mode = StationMode::Replace;
    std::string station = "RADIO_02_POP";
    std::string fallback_station = "RADIO_01_CLASS_ROCK";
    // Ask the game to un-hide the station on the wheel.
    bool unlock_station = true;
    // Allow the station to keep playing on foot through the phone radio.
    bool mobile_radio = true;
    // Replace mode: how to silence the station we are standing in for, in
    // order. Scene and Freeze leave the radio wheel untouched; VehicleOff and
    // DisableRadio are heavier and can take the station off the wheel while
    // they are applied.
    //   Scene        - start a stock audio scene that mutes the radio
    //   Freeze       - stop the station's own timeline
    //   VehicleOff   - hold the vehicle's radio on OFF
    //   DisableRadio - disable the vehicle's radio outright
    std::string mute_strategies = "Scene,Freeze,VehicleOff";
    // Replace mode: audio scenes tried in order until one reports active.
    std::string mute_scenes =
        "MP_JOB_CHANGE_RADIO_MUTE,FBI_HEIST_H5_MUTE_RADIO_SCENE,"
        "CHARACTER_CHANGE_IN_SKY_SCENE";

    // [SelfRadio]
    bool generate_silent_track = true;
    int silent_track_minutes = 30;
    // Empty means "probe the usual Documents locations".
    std::wstring user_music_dir;

    // [Audio]
    // Trim applied on top of Spotify's loudness normalisation. Our voice goes
    // out at the master mix level while a native station sits well below it,
    // so this is a cut, not a boost. It is a by-ear calibration -- the volume
    // hotkeys move it live.
    float volume_db = -9.0f;
    // Track the in-game Music slider through GET_MUSIC_VOL_SLIDER.
    bool follow_music_slider = true;
    // Exponent mapping the 0..10 slider onto linear amplitude.
    float slider_curve = 2.0f;
    // 0..10 to pin the slider manually, or -1 to read it from the game.
    int music_slider_override = -1;
    // Connect device volume (0..65535) the Spotify app starts at.
    int spotify_volume = 65535;
    bool replaygain = true;
    // Local PCM queue depth. The library runs ~500 ms ahead of wall clock.
    int buffer_ms = 700;

    // [Mix]
    bool duck_on_dialogue = true;
    float dialogue_duck_db = -11.0f;
    // Level heard from outside the vehicle, at the vehicle itself.
    float exterior_gain_db = -7.0f;
    float exterior_range_m = 16.0f;
    // Follow the game's own radio fade (mission mutes, retune sweeps). Turn
    // this off if Replace mode leaves the station permanently silent.
    bool respect_game_radio_fade = true;
    bool cabin_filter = true;
    // Low-pass corner inside a closed cabin and outside the vehicle.
    float interior_cutoff_hz = 15000.0f;
    float exterior_cutoff_hz = 2600.0f;
    float submerged_cutoff_hz = 700.0f;
    // Fade applied when the radio starts, stops or is ducked.
    int fade_ms = 90;
    // Hold the Spotify stream too, once the radio has been silent this long.
    int stream_pause_delay_ms = 1200;

    // [Controls]
    int key_toggle = VK_F7;
    int key_play_pause = VK_F9;
    int key_next = VK_F10;
    int key_previous = VK_F8;
    int key_volume_up = VK_PRIOR;
    int key_volume_down = VK_NEXT;
    float volume_step_db = 1.0f;

    static Settings load(const std::filesystem::path& ini_path);
};

float db_to_linear(float db);

} // namespace gsr

#pragma once

#include "game_audio.h"
#include "settings.h"
#include "xaudio_player.h"

#include <librespotc/librespotc.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gsr {

class SpotifyRadio {
public:
    explicit SpotifyRadio(std::filesystem::path module_directory);
    ~SpotifyRadio();

    bool initialize();
    void tick();

private:
    void connect_worker();
    void prepare_self_radio();
    void apply_persistent_audio_flags();

    void update_ownership();
    void set_owned(bool owned);
    void acquire_station();
    void release_station();
    void apply_frame_mutes();
    // Vehicle handle, or 0. Typed as int so this header need not pull in
    // the ScriptHookV types.
    int player_vehicle() const;

    void update_stream(const RadioSnapshot& snapshot);
    void update_hotkeys();
    void publish_pending_notification();
    void notify(const std::string& text);
    void tune_to(const std::string& station);

    std::filesystem::path module_directory_;
    std::filesystem::path data_directory_;
    Settings settings_;
    GameAudioProbe probe_;
    XAudioPlayer player_;

    std::unique_ptr<librespotc::Session> session_;
    std::thread connect_thread_;
    std::mutex session_mutex_;
    std::mutex notification_mutex_;
    std::string pending_notification_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> spotify_playing_{false};
    // Set when the pause came from the Spotify app rather than from us, so we
    // do not fight the user by resuming behind their back.
    std::atomic<bool> paused_by_user_{false};

    bool announced_ready_ = false;
    bool station_held_ = false;
    bool was_retuning_ = false;
    // Set when our own muting has taken the station off the radio wheel.
    bool station_reads_off_ = false;
    bool mute_with_scene_ = false;
    bool mute_with_freeze_ = false;
    bool mute_with_vehicle_off_ = false;
    bool mute_with_disable_radio_ = false;
    bool stream_running_ = false;
    std::string active_mute_scene_;
    std::string previous_station_ = "RADIO_01_CLASS_ROCK";
    int64_t silent_since_ms_ = 0;
    int64_t last_flag_refresh_ms_ = 0;
};

} // namespace gsr

#pragma once

#include "xaudio_player.h"

#include <librespotc/librespotc.h>

#include <atomic>
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
    void update_station_state();
    void update_hotkeys();
    void publish_pending_notification();
    void notify(const std::string& text);
    bool is_spotify_station_selected() const;

    std::filesystem::path module_directory_;
    std::filesystem::path data_directory_;
    std::unique_ptr<librespotc::Session> session_;
    std::thread connect_thread_;
    std::mutex session_mutex_;
    std::mutex notification_mutex_;
    XAudioPlayer player_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> spotify_playing_{false};
    bool station_active_ = false;
    bool manual_station_active_ = false;
    bool session_station_state_applied_ = false;
    bool announced_ready_ = false;
    std::string pending_notification_;
    std::string station_name_ = "RADIO_19_USER";
    std::string previous_station_name_ = "RADIO_01_CLASS_ROCK";
    int select_station_key_ = VK_F7;
    int play_pause_key_ = VK_F9;
    int next_key_ = VK_F10;
    int previous_key_ = VK_F8;
};

} // namespace gsr

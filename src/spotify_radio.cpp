#include "spotify_radio.h"

#include "log.h"

#include <Windows.h>
#include <natives.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <sstream>
#include <utility>

namespace {

std::string narrow_ascii(const std::wstring& input) {
    return std::string(input.begin(), input.end());
}

} // namespace

namespace gsr {

SpotifyRadio::SpotifyRadio(std::filesystem::path module_directory)
    : module_directory_(std::move(module_directory)),
      data_directory_(module_directory_ / "GtaSpotifyRadio") {}

SpotifyRadio::~SpotifyRadio() {
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(session_mutex_);
        if (session_) session_->disconnect();
    }
    if (connect_thread_.joinable()) connect_thread_.join();
}

bool SpotifyRadio::initialize() {
    log::initialize(data_directory_);
    log::info("Initializing GTA Spotify Radio prototype");

    const auto ini = module_directory_ / "GtaSpotifyRadio.ini";
    wchar_t station[64] = L"RADIO_19_USER";
    GetPrivateProfileStringW(L"Radio", L"Station", L"RADIO_19_USER",
                             station, static_cast<DWORD>(std::size(station)),
                             ini.c_str());
    station_name_ = narrow_ascii(station);
    select_station_key_ = GetPrivateProfileIntW(L"Controls", L"SelectStation", VK_F7,
                                                 ini.c_str());
    play_pause_key_ = GetPrivateProfileIntW(L"Controls", L"PlayPause", VK_F9,
                                            ini.c_str());
    next_key_ = GetPrivateProfileIntW(L"Controls", L"Next", VK_F10,
                                      ini.c_str());
    previous_key_ = GetPrivateProfileIntW(L"Controls", L"Previous", VK_F8,
                                          ini.c_str());
    const int volume_percent = GetPrivateProfileIntW(
        L"Audio", L"VolumePercent", 80, ini.c_str());

    if (!player_.initialize()) {
        log::error("Audio initialization failed");
        return false;
    }
    player_.set_volume(std::clamp(volume_percent, 0, 200) / 100.0f);
    player_.set_active(false);

    connect_thread_ = std::thread(&SpotifyRadio::connect_worker, this);
    notify("Spotify Radio loaded - press F7 in a vehicle, then choose GTA V Radio in Spotify");
    return true;
}

void SpotifyRadio::connect_worker() {
    librespotc::Config config;
    config.device_name = "GTA V Radio";
    config.cache_dir = (data_directory_ / "cache").string();
    config.bitrate = librespotc::Bitrate::K320;
    config.apply_volume_gain = true;
    config.apply_replaygain = true;
    config.initial_volume = 49151;
    config.on_audio = [this](const int16_t* pcm, size_t frames,
                             const librespotc::AudioFormat& format) {
        if (stopping_.load(std::memory_order_acquire)) return false;
        return player_.submit(pcm, frames, format);
    };
    config.on_track_change = [this](const librespotc::TrackInfo& track) {
        const std::string title = track.artist.empty()
            ? track.title
            : track.artist + " - " + track.title;
        log::info("Now playing: " + title);
        std::lock_guard lock(notification_mutex_);
        pending_notification_ = "Spotify: " + title;
    };
    config.on_event = [this](const librespotc::Event& event) {
        if (event.type == librespotc::EventType::PlaybackStarted) {
            spotify_playing_.store(true, std::memory_order_release);
        } else if (event.type == librespotc::EventType::PlaybackPaused ||
                   event.type == librespotc::EventType::BecameInactive) {
            spotify_playing_.store(false, std::memory_order_release);
        }
        if (!event.detail.empty()) log::info("Spotify event: " + event.detail);
    };

    auto session = librespotc::Session::create(config);
    if (!session) {
        log::error("Could not create Spotify Connect session");
        return;
    }
    log::info("Advertising Spotify Connect device 'GTA V Radio'");
    if (!session->connect()) {
        log::error("Spotify connection failed: " + session->last_error_message());
        return;
    }
    if (stopping_.load(std::memory_order_acquire)) {
        session->disconnect();
        return;
    }

    {
        std::lock_guard lock(session_mutex_);
        session_ = std::move(session);
    }
    connected_.store(true, std::memory_order_release);
    log::info("Spotify Connect authenticated and ready");
}

void SpotifyRadio::tick() {
    update_station_state();
    update_hotkeys();
    publish_pending_notification();

    if (connected_.load(std::memory_order_acquire) && !announced_ready_) {
        announced_ready_ = true;
        notify("Spotify connected - GTA V Radio is ready");
    }
}

bool SpotifyRadio::is_spotify_station_selected() const {
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(player, false)) return false;
    if (manual_station_active_) return true;
    const char* current = AUDIO::GET_PLAYER_RADIO_STATION_NAME();
    return current && station_name_ == current;
}

void SpotifyRadio::update_station_state() {
    const bool active = is_spotify_station_selected();
    const bool changed = active != station_active_;
    if (changed) {
        station_active_ = active;
        player_.set_active(active);
        log::info(std::string("Spotify station state: ") + (active ? "active" : "inactive"));
    }
    if (!changed && session_station_state_applied_) return;

    std::lock_guard lock(session_mutex_);
    if (!session_) return;
    session_station_state_applied_ = true;
    if (active) {
        session_->resume();
        notify("Spotify Radio on");
    } else {
        session_->pause_at_audio_boundary();
        notify("Spotify Radio paused");
    }
}

void SpotifyRadio::update_hotkeys() {
    if (GetAsyncKeyState(select_station_key_) & 1) {
        const Ped player = PLAYER::PLAYER_PED_ID();
        if (!PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
            notify("Enter a vehicle before selecting Spotify Radio");
        } else {
            const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(player, false);
            if (manual_station_active_) {
                manual_station_active_ = false;
                AUDIO::SET_VEH_RADIO_STATION(vehicle, previous_station_name_.c_str());
                AUDIO::SET_RADIO_TO_STATION_NAME(previous_station_name_.c_str());
                log::info("Spotify station disabled with select hotkey");
                notify("Spotify Radio off");
            } else {
                const char* current = AUDIO::GET_PLAYER_RADIO_STATION_NAME();
                if (current && std::strcmp(current, "OFF") != 0) {
                    previous_station_name_ = current;
                }
                manual_station_active_ = true;
                AUDIO::SET_VEH_RADIO_STATION(vehicle, "OFF");
                AUDIO::SET_RADIO_TO_STATION_NAME("OFF");
                log::info("Spotify station enabled with select hotkey");
                notify("Spotify Radio on");
            }
        }
    }

    std::lock_guard lock(session_mutex_);
    if (!session_) return;

    if (GetAsyncKeyState(play_pause_key_) & 1) {
        const auto track = session_->current_track();
        if (track.track_id.empty()) {
            notify("Choose GTA V Radio from Spotify's device list first");
        } else if (spotify_playing_.load(std::memory_order_acquire)) {
            session_->pause();
            notify("Spotify paused");
        } else {
            session_->resume();
            notify("Spotify resumed");
        }
    }
    if (GetAsyncKeyState(next_key_) & 1) {
        if (!session_->next()) notify("Use Spotify to choose the next track");
    }
    if (GetAsyncKeyState(previous_key_) & 1) {
        if (!session_->previous()) notify("No previous Spotify track is available");
    }
}

void SpotifyRadio::publish_pending_notification() {
    std::string message;
    {
        std::lock_guard lock(notification_mutex_);
        message.swap(pending_notification_);
    }
    if (!message.empty()) notify(message);
}

void SpotifyRadio::notify(const std::string& text) {
    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text.c_str());
    HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(false, false);
}

} // namespace gsr

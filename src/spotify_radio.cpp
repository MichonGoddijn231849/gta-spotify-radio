#include "spotify_radio.h"

#include "log.h"
#include "silent_track.h"

#include <Windows.h>
#include <natives.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

namespace {

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const size_t first = item.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const size_t last = item.find_last_not_of(" \t");
        items.push_back(item.substr(first, last - first + 1));
    }
    return items;
}

} // namespace

namespace gsr {

SpotifyRadio::SpotifyRadio(std::filesystem::path module_directory)
    : module_directory_(std::move(module_directory)),
      data_directory_(module_directory_ / "GtaSpotifyRadio") {}

SpotifyRadio::~SpotifyRadio() {
    // Natives are only safe on the script thread, and this destructor is not
    // guaranteed to run there, so the station takeover is left as-is.
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(session_mutex_);
        if (session_) session_->disconnect();
    }
    if (connect_thread_.joinable()) connect_thread_.join();
    player_.shutdown();
}

bool SpotifyRadio::initialize() {
    log::initialize(data_directory_);
    log::info("Initializing GTA Spotify Radio");

    settings_ = Settings::load(module_directory_ / "GtaSpotifyRadio.ini");
    previous_station_ = settings_.fallback_station;

    for (const std::string& strategy : split_list(settings_.mute_strategies)) {
        if (_stricmp(strategy.c_str(), "Scene") == 0) mute_with_scene_ = true;
        else if (_stricmp(strategy.c_str(), "Freeze") == 0) mute_with_freeze_ = true;
        else if (_stricmp(strategy.c_str(), "VehicleOff") == 0) mute_with_vehicle_off_ = true;
        else if (_stricmp(strategy.c_str(), "DisableRadio") == 0) mute_with_disable_radio_ = true;
        else log::error("Unknown Radio/MuteStrategies entry '" + strategy + "'");
    }

    if (!player_.initialize(settings_.fade_ms, settings_.buffer_ms, settings_.cabin_filter)) {
        log::error("Audio initialization failed");
        return false;
    }

    if (settings_.mode == StationMode::SelfRadio) prepare_self_radio();
    apply_persistent_audio_flags();

    if (settings_.unlock_station) {
        AUDIO::LOCK_RADIO_STATION(settings_.station.c_str(), FALSE);
    }
    AUDIO::SET_USER_RADIO_CONTROL_ENABLED(TRUE);

    connect_thread_ = std::thread(&SpotifyRadio::connect_worker, this);
    notify("Spotify Radio loaded - select GTA V Radio in the Spotify app");
    return true;
}

void SpotifyRadio::prepare_self_radio() {
    if (!settings_.generate_silent_track) return;

    const auto dir = find_user_music_dir(settings_.user_music_dir);
    const auto result = ensure_silent_track(dir, settings_.silent_track_minutes);
    if (!result.ok) {
        log::error("Self Radio placeholder unavailable: " + result.detail);
        notify("Self Radio placeholder failed - see GtaSpotifyRadio.log");
        return;
    }
    log::info("Self Radio placeholder: " + result.detail + " (" + result.path.string() + ")");
    if (result.created) {
        notify("Placeholder track added - rescan user music in Settings > Audio");
    }
}

void SpotifyRadio::apply_persistent_audio_flags() {
    last_flag_refresh_ms_ = now_ms();
    if (!settings_.mobile_radio) return;
    AUDIO::SET_AUDIO_FLAG("MobileRadioInGame", TRUE);
    AUDIO::SET_AUDIO_FLAG("AllowRadioDuringSwitch", TRUE);
    AUDIO::SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY(TRUE);
}

void SpotifyRadio::connect_worker() {
    librespotc::Config config;
    config.device_name = "GTA V Radio";
    config.cache_dir = (data_directory_ / "cache").string();
    config.bitrate = librespotc::Bitrate::K320;
    config.apply_volume_gain = true;
    config.apply_replaygain = settings_.replaygain;
    config.initial_volume = static_cast<uint32_t>(settings_.spotify_volume);
    config.on_audio = [this](const int16_t* pcm, size_t frames,
                             const librespotc::AudioFormat& format) {
        if (stopping_.load(std::memory_order_acquire)) return false;
        return player_.submit(pcm, frames, format);
    };
    config.on_track_change = [this](const librespotc::TrackInfo& track) {
        const std::string title =
            track.artist.empty() ? track.title : track.artist + " - " + track.title;
        log::info("Now playing: " + title);
        std::lock_guard lock(notification_mutex_);
        pending_notification_ = "Spotify: " + title;
    };
    config.on_event = [this](const librespotc::Event& event) {
        const bool from_cloud = event.source == librespotc::EventSource::Cloud;
        switch (event.type) {
            case librespotc::EventType::PlaybackStarted:
                spotify_playing_.store(true, std::memory_order_release);
                if (from_cloud) paused_by_user_.store(false, std::memory_order_release);
                break;
            case librespotc::EventType::PlaybackPaused:
                spotify_playing_.store(false, std::memory_order_release);
                if (from_cloud) paused_by_user_.store(true, std::memory_order_release);
                break;
            case librespotc::EventType::BecameInactive:
                spotify_playing_.store(false, std::memory_order_release);
                break;
            default:
                break;
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
    player_.heartbeat();

    update_ownership();

    StationState station;
    station.owned = station_held_;
    station.forced_off = station_reads_off_;
    const RadioSnapshot snapshot = probe_.poll(settings_, station);

    MixTarget target;
    target.playing = snapshot.station_selected && !snapshot.held;
    target.gain = target.playing ? snapshot.gain : 0.0f;
    target.cutoff_hz = snapshot.cutoff_hz;
    player_.set_target(target);

    update_stream(snapshot);
    update_hotkeys();
    publish_pending_notification();

    const int64_t now = now_ms();
    if (now - last_flag_refresh_ms_ > 2000) apply_persistent_audio_flags();

    if (connected_.load(std::memory_order_acquire) && !announced_ready_) {
        announced_ready_ = true;
        notify("Spotify connected - tune to " + settings_.station + " on the radio wheel");
    }
}

void SpotifyRadio::set_owned(bool owned) {
    if (owned == station_held_) return;
    station_held_ = owned;
    if (owned) {
        acquire_station();
    } else {
        release_station();
    }
}

// Deciding who owns the radio has to account for our own muting: silencing the
// station we stand in for means switching the radio to OFF, so the game stops
// reporting our station back to us and we cannot simply read the selection.
//
// The radio wheel stays usable because we let go of the radio for as long as
// the player is actually retuning, and only re-read the selection once they
// have settled on something.
void SpotifyRadio::update_ownership() {
    const bool retuning = AUDIO::IS_RADIO_RETUNING() != 0;
    const std::string current = current_station_name();
    const bool settled = was_retuning_ && !retuning;
    was_retuning_ = retuning;

    if (retuning) {
        // Hands off the radio: whatever the player picks must stick.
        return;
    }

    if (settled) {
        // The player just chose a station, so the selection is theirs alone.
        set_owned(current == settings_.station);
    } else if (!station_held_) {
        set_owned(current == settings_.station);
    } else if (!station_reads_off_ && current != settings_.station) {
        // Something else retuned us without going through the wheel.
        set_owned(false);
    }

    if (station_held_ && settings_.mode == StationMode::Replace) apply_frame_mutes();
}

int SpotifyRadio::player_vehicle() const {
    const Ped player = PLAYER::PLAYER_PED_ID();
    return PED::GET_VEHICLE_PED_IS_IN(player, false);
}

// The heavier strategies have to be re-asserted, because the game turns the
// radio back on by itself when the player changes vehicle.
void SpotifyRadio::apply_frame_mutes() {
    const Vehicle vehicle = player_vehicle();
    if (vehicle == 0) return;
    if (mute_with_disable_radio_) AUDIO::SET_VEHICLE_RADIO_ENABLED(vehicle, FALSE);
    if (mute_with_vehicle_off_) AUDIO::SET_VEH_RADIO_STATION(vehicle, "OFF");
}

void SpotifyRadio::acquire_station() {
    log::info("Tuned to " + settings_.station);
    if (settings_.mode != StationMode::Replace) return;

    if (mute_with_freeze_) {
        AUDIO::SET_RADIO_AUTO_UNFREEZE(FALSE);
        AUDIO::FREEZE_RADIO_STATION(settings_.station.c_str());
    }

    if (mute_with_scene_) {
        bool started = false;
        for (const std::string& scene : split_list(settings_.mute_scenes)) {
            AUDIO::START_AUDIO_SCENE(scene.c_str());
            if (AUDIO::IS_AUDIO_SCENE_ACTIVE(scene.c_str())) {
                active_mute_scene_ = scene;
                log::info("Mute audio scene started: " + scene);
                started = true;
                break;
            }
            AUDIO::STOP_AUDIO_SCENE(scene.c_str());
            log::info("Mute audio scene not available: " + scene);
        }
        if (!started) {
            log::error("No mute audio scene started. If the native station is still "
                       "audible under Spotify, add VehicleOff to Radio/MuteStrategies.");
        }
    }

    apply_frame_mutes();

    // Record whether our own muting hid the station, so the rest of the
    // plugin keeps treating a reported "OFF" as us rather than going silent.
    const std::string after = current_station_name();
    station_reads_off_ = after == "OFF";
    log::info("Station reads '" + after + "' after muting" +
              (station_reads_off_ ? " (holding it as ours)" : ""));
}

void SpotifyRadio::release_station() {
    station_reads_off_ = false;
    if (!active_mute_scene_.empty()) {
        AUDIO::STOP_AUDIO_SCENE(active_mute_scene_.c_str());
        active_mute_scene_.clear();
    }
    if (settings_.mode != StationMode::Replace) return;

    if (mute_with_freeze_) {
        AUDIO::UNFREEZE_RADIO_STATION(settings_.station.c_str());
        AUDIO::SET_RADIO_AUTO_UNFREEZE(TRUE);
    }
    const Vehicle vehicle = player_vehicle();
    if (vehicle != 0 && mute_with_disable_radio_) {
        AUDIO::SET_VEHICLE_RADIO_ENABLED(vehicle, TRUE);
    }
}

void SpotifyRadio::update_stream(const RadioSnapshot& snapshot) {
    std::lock_guard lock(session_mutex_);
    if (!session_) return;

    const bool wants_audio = snapshot.station_selected && !snapshot.held;
    const int64_t now = now_ms();

    if (wants_audio) {
        silent_since_ms_ = 0;
        if (!stream_running_ && !paused_by_user_.load(std::memory_order_acquire)) {
            session_->resume();
            stream_running_ = true;
        }
        return;
    }

    if (!stream_running_) return;
    // Hold the stream only once our own output has genuinely faded out, so a
    // brief interruption never clips a track mid-word.
    if (silent_since_ms_ == 0) {
        if (!player_.is_silent()) return;
        silent_since_ms_ = now;
    }
    if (now - silent_since_ms_ >= settings_.stream_pause_delay_ms) {
        session_->pause_at_audio_boundary();
        stream_running_ = false;
    }
}

void SpotifyRadio::tune_to(const std::string& station) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (vehicle != 0) AUDIO::SET_VEH_RADIO_STATION(vehicle, station.c_str());
    AUDIO::SET_RADIO_TO_STATION_NAME(station.c_str());
}

void SpotifyRadio::update_hotkeys() {
    if (GetAsyncKeyState(settings_.key_toggle) & 1) {
        if (station_held_) {
            // Let go of the radio before retuning, so the restore is not
            // fighting our own muting.
            set_owned(false);
            tune_to(previous_station_);
            notify("Radio: " + previous_station_);
        } else {
            const std::string current = current_station_name();
            if (current != "OFF") previous_station_ = current;
            tune_to(settings_.station);
            set_owned(true);
            notify("Radio: Spotify");
        }
    }

    std::lock_guard lock(session_mutex_);
    if (!session_) return;

    if (GetAsyncKeyState(settings_.key_play_pause) & 1) {
        const auto track = session_->current_track();
        if (track.track_id.empty()) {
            notify("Choose GTA V Radio from Spotify's device list first");
        } else if (spotify_playing_.load(std::memory_order_acquire)) {
            session_->pause();
            paused_by_user_.store(true, std::memory_order_release);
            stream_running_ = false;
            notify("Spotify paused");
        } else {
            paused_by_user_.store(false, std::memory_order_release);
            session_->resume();
            stream_running_ = true;
            notify("Spotify resumed");
        }
    }
    if (GetAsyncKeyState(settings_.key_next) & 1) {
        if (!session_->next()) notify("Use Spotify to choose the next track");
    }
    if (GetAsyncKeyState(settings_.key_previous) & 1) {
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

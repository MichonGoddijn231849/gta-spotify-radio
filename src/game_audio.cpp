#include "game_audio.h"

#include "log.h"

#include <Windows.h>
#include <natives.h>

#include <algorithm>
#include <cmath>

namespace {

float distance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Anything that makes the game itself stop the radio dead rather than just
// turn it down.
bool game_holds_audio() {
    if (HUD::IS_PAUSE_MENU_ACTIVE()) return true;
    if (CUTSCENE::IS_CUTSCENE_PLAYING()) return true;
    if (STREAMING::IS_PLAYER_SWITCH_IN_PROGRESS()) return true;
    if (CAM::IS_SCREEN_FADED_OUT()) return true;
    return false;
}

bool anyone_is_talking(Ped player) {
    if (AUDIO::IS_SCRIPTED_CONVERSATION_ONGOING()) return true;
    if (AUDIO::IS_MOBILE_PHONE_CALL_ONGOING()) return true;
    if (AUDIO::IS_ANY_SPEECH_PLAYING(player)) return true;
    if (AUDIO::IS_AMBIENT_SPEECH_PLAYING(player)) return true;
    return false;
}

// The cabin is only sealed on a car with the roof up. Bikes, boats, open
// vehicles and convertibles with the roof down get the full-range signal.
bool cabin_is_open(Vehicle vehicle) {
    const int roof_state = VEHICLE::GET_CONVERTIBLE_ROOF_STATE(vehicle);
    // 0 = roof up, 1 = lowering, 2 = down, 3 = raising, 5 = no roof.
    if (roof_state == 1 || roof_state == 2 || roof_state == 3) return true;

    switch (VEHICLE::GET_VEHICLE_CLASS(vehicle)) {
        case 8:  // motorcycles
        case 13: // bicycles
        case 14: // boats
            return true;
        default:
            return false;
    }
}

} // namespace

namespace gsr {

std::string current_station_name() {
    const char* name = AUDIO::GET_PLAYER_RADIO_STATION_NAME();
    return name ? std::string(name) : std::string("OFF");
}

float GameAudioProbe::slider_gain(const Settings& settings) {
    if (!settings.follow_music_slider && settings.music_slider_override < 0) {
        return 1.0f;
    }

    int raw = settings.music_slider_override;
    if (raw < 0) raw = AUDIO::GET_MUSIC_VOL_SLIDER();
    if (raw != music_slider_) {
        music_slider_ = raw;
        if (!logged_slider_) {
            logged_slider_ = true;
            log::info("Music volume slider reads " + std::to_string(raw));
        }
    }

    // The slider is documented as 0..10 but treat a 0..100 build gracefully
    // rather than clamping every value above 10 to full volume.
    float normalized;
    if (raw < 0) {
        normalized = 1.0f;
    } else if (raw <= 10) {
        normalized = raw / 10.0f;
    } else if (raw <= 100) {
        normalized = raw / 100.0f;
    } else {
        normalized = 1.0f;
    }
    return std::pow(std::clamp(normalized, 0.0f, 1.0f), settings.slider_curve);
}

RadioSnapshot GameAudioProbe::poll(const Settings& settings, bool own_off_station) {
    RadioSnapshot snap;

    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(player)) return snap;

    const std::string current = current_station_name();
    snap.station_selected =
        current == settings.station || (own_off_station && current == "OFF");
    if (!snap.station_selected) return snap;

    Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(player, false);
    const bool in_vehicle = vehicle != 0 && ENTITY::DOES_ENTITY_EXIST(vehicle);
    if (!in_vehicle) {
        // Still audible from outside the car the player just stepped out of.
        vehicle = PLAYER::GET_PLAYERS_LAST_VEHICLE();
        if (vehicle != 0 && !ENTITY::DOES_ENTITY_EXIST(vehicle)) vehicle = 0;
    }
    snap.vehicle = vehicle;

    const bool phone_radio = !in_vehicle && settings.mobile_radio &&
                             AUDIO::IS_MOBILE_PHONE_RADIO_ACTIVE();
    if (vehicle == 0 && !phone_radio) return snap;

    if (vehicle != 0) {
        // Once we have switched the native radio off ourselves, its own
        // on/off state no longer says anything about what we should do.
        if (!own_off_station && !AUDIO::IS_VEHICLE_RADIO_ON(vehicle)) return snap;
        if (!VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle, false)) return snap;
        if (!VEHICLE::GET_IS_VEHICLE_ENGINE_RUNNING(vehicle)) return snap;
    }

    snap.held = game_holds_audio();
    // The game's own radio fade covers mission mutes and the retune sweep.
    // Treat those as a duck rather than a hold so the stream keeps rolling.
    // A radio we switched off ourselves reads as permanently faded out, which
    // would otherwise silence us for good.
    const bool game_faded = settings.respect_game_radio_fade && !own_off_station &&
                            AUDIO::IS_RADIO_FADED_OUT() != 0;
    const bool retuning = AUDIO::IS_RADIO_RETUNING() != 0;

    float gain = db_to_linear(settings.volume_db) * slider_gain(settings);

    if (game_faded || retuning) gain = 0.0f;

    if (settings.duck_on_dialogue && anyone_is_talking(player)) {
        snap.ducking = true;
        gain *= db_to_linear(settings.dialogue_duck_db);
    }

    float cutoff = settings.cabin_filter && vehicle != 0 && !cabin_is_open(vehicle)
                       ? settings.interior_cutoff_hz
                       : 22050.0f;

    if (!in_vehicle && vehicle != 0) {
        snap.listener_outside = true;
        const Vector3 camera = CAM::GET_GAMEPLAY_CAM_COORD();
        const Vector3 source = ENTITY::GET_ENTITY_COORDS(vehicle, true);
        const float d = distance(camera, source);

        constexpr float kNear = 2.0f;
        const float span = std::max(settings.exterior_range_m - kNear, 1.0f);
        const float rolloff = std::clamp(1.0f - (d - kNear) / span, 0.0f, 1.0f);

        gain *= rolloff * db_to_linear(settings.exterior_gain_db);
        if (settings.cabin_filter) {
            // Fully muffled at the far edge, close to open at the window.
            cutoff = std::min(cutoff, settings.exterior_cutoff_hz +
                                          (22050.0f - settings.exterior_cutoff_hz) *
                                              rolloff * rolloff);
        }
    }

    const Entity listener = in_vehicle && vehicle != 0 ? vehicle : player;
    const float submerged = ENTITY::GET_ENTITY_SUBMERGED_LEVEL(listener);
    if (submerged > 0.35f && settings.cabin_filter) {
        const float wet = std::clamp((submerged - 0.35f) / 0.65f, 0.0f, 1.0f);
        cutoff = std::min(cutoff, settings.submerged_cutoff_hz +
                                      (cutoff - settings.submerged_cutoff_hz) * (1.0f - wet));
        gain *= 1.0f - 0.5f * wet;
    }

    snap.gain = std::clamp(gain, 0.0f, 4.0f);
    snap.cutoff_hz = std::clamp(cutoff, 100.0f, 22050.0f);
    return snap;
}

} // namespace gsr

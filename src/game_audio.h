#pragma once

#include "settings.h"

#include <string>

namespace gsr {

// One frame's worth of the game's own radio mixer state, reduced to the
// handful of values the output stage needs.
struct RadioSnapshot {
    // Our station is the one the player is tuned to.
    bool station_selected = false;
    // The radio should be silent and held where it is: pause menu, cutscene,
    // screen fade, character switch, or the game fading the radio out itself.
    bool held = false;
    // Linear output gain, already including the Music slider, dialogue
    // ducking and the outside-the-car rolloff.
    float gain = 0.0f;
    // Low-pass corner emulating cabin, exterior and underwater muffling.
    float cutoff_hz = 22050.0f;
    // Diagnostics for the log, not used by the mixer.
    bool ducking = false;
    bool listener_outside = false;
    int vehicle = 0;
};

// Reads the game each frame and reports what a native radio station would be
// doing right now.
class GameAudioProbe {
public:
    RadioSnapshot poll(const Settings& settings);

    int music_slider() const { return music_slider_; }

private:
    float slider_gain(const Settings& settings);

    int music_slider_ = -1;
    bool logged_slider_ = false;
};

// The station the player is currently tuned to, or "OFF".
std::string current_station_name();

} // namespace gsr

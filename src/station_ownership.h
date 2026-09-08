#pragma once

#include <string>

namespace gsr {

// Everything needed to decide whether Spotify is the station the player is
// listening to. Kept free of game calls so the rule can be reasoned about and
// tested on its own -- it is subtle enough that reading it out of the game
// state inline got it wrong twice.
struct OwnershipInputs {
    // The station the game reports right now, or "OFF".
    std::string current_station;
    // The station we stand in for.
    std::string our_station;
    // The player is working the radio wheel this frame.
    bool retuning = false;
    // Whether we own the radio going into this frame.
    bool owned = false;
    // Our own muting is the reason the radio reads OFF. Only ever true while
    // we own it, so a reported "OFF" is unambiguous.
    bool holding_off = false;
};

// Whether Spotify should own the radio after this frame.
bool decide_ownership(const OwnershipInputs& in);

} // namespace gsr

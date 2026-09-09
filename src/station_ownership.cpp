#include "station_ownership.h"

namespace gsr {

bool decide_ownership(const OwnershipInputs& in) {
    // While the player is working the wheel we keep our hands off the radio
    // entirely, so whatever they land on sticks. Re-reading the selection
    // mid-retune would fight them.
    if (in.retuning) return in.owned;

    // Otherwise the selection decides, with one wrinkle: silencing the station
    // we stand in for means switching the radio to OFF, so while we are the
    // ones holding it there, "OFF" still means us.
    if (in.current_station == in.our_station) return true;
    return in.holding_off && in.current_station == "OFF";
}

} // namespace gsr

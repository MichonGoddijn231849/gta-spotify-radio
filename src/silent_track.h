#pragma once

#include <filesystem>
#include <string>

namespace gsr {

// Self Radio only shows up on the radio wheel when the user music folder has
// something in it, and whatever it finds there is what the native station
// plays. Dropping in one long silent MP3 gives us a real native station whose
// own output is inaudible, which is what lets this plugin supply the audio
// while the game keeps running its normal radio state machine.
struct SilentTrackResult {
    bool ok = false;
    bool created = false;
    std::filesystem::path path;
    std::string detail;
};

// Returns the GTA user music folder, or an empty path if none was found.
// override_dir wins when it is non-empty.
std::filesystem::path find_user_music_dir(const std::wstring& override_dir);

SilentTrackResult ensure_silent_track(const std::filesystem::path& user_music_dir,
                                      int minutes);

} // namespace gsr

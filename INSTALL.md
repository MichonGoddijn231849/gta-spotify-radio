# GTA Spotify Radio

An experimental Story Mode plugin for GTA V Enhanced. It creates a Spotify
Connect receiver named **GTA V Radio** and plays it as a station on the radio
wheel, following the game's own radio behaviour: the pause menu pauses it, it
ducks under dialogue, it follows the Music volume slider, and it is muffled
inside the cabin and from outside the car.

## Install

1. Copy `GtaSpotifyRadio.asi` and `GtaSpotifyRadio.ini` into the GTA V Enhanced
   folder beside `GTA5_Enhanced.exe`.
2. In Spotify, open the device picker and select **GTA V Radio**, then start a
   song or playlist.
3. Start GTA V in Story Mode, get in a vehicle, and tune the radio wheel to
   **Pop** (`RADIO_02_POP`), or press **F7**.

Spotify plays in that station's place. Pick a different station to stand in
for with `Station` in `GtaSpotifyRadio.ini`.

### Why the wheel reads OFF while Spotify plays

No native mutes a radio station while it stays selected, so silencing the
station this plugin stands in for means switching the vehicle radio to OFF.
The radio wheel still works normally: the plugin lets go of the radio for as
long as you are retuning, and only takes it back once you settle on its
station again. Pick any other station and the native radio plays as usual.

The cost is cosmetic. While Spotify is playing the wheel and HUD read OFF
rather than the station name.

### Self Radio

`Mode=SelfRadio` is still in the ini but does not work on GTA V Enhanced: the
game never indexes user music, so Self Radio stays off the radio wheel even
with ordinary MP3s in the folder. It is kept for Legacy only.

The first Spotify pairing is cached in
`GtaSpotifyRadio\cache\credentials.dat`. Treat this file as private account
data.

## Controls

- F7: tune to the Spotify station, or back to the previous one
- F9: pause/resume
- F10: next track in the received Spotify context
- F8: previous track

## Tuning it

Everything below lives in `GtaSpotifyRadio.ini`.

- **You can hear the native station under Spotify** — add `VehicleOff` to
  `MuteStrategies`, as above.
- **Nothing plays at all** — check `GtaSpotifyRadio.log` for the station name
  it sees, then try `RespectGameRadioFade=0` under `[Mix]`.
- **Too quiet or too loud next to the other stations** — adjust `VolumeDb`
  under `[Audio]`. It is a straight dB trim on top of Spotify's loudness
  normalisation.
- **Music slider does nothing** — check `FollowMusicSlider=1`, or pin a level
  with `MusicSliderOverride` (0-10).
- **Ducking under dialogue is too strong or too weak** — `DialogueDuckDb`.
- **Sounds dull** — `CabinFilter=0` turns the muffling off entirely, or raise
  `InteriorCutoffHz`.
- **Audio drops out on a stutter** — raise `BufferMs`.
- **Track skips a second when unpausing** — lower `BufferMs`.

## Current limitations

The plugin owns an XAudio2 voice inside the GTA process; it does not hook the
proprietary RAGE radio mixer. Everything listed above is emulated by reading
the game each frame rather than inherited from the mixer, so interior and
tunnel reverb and the mixer's own compression are approximated. The station
name and wheel icon are whichever native station is hosting it.

## Safety

Use only in Story Mode with BattlEye disabled. Do not use ASI plugins in GTA
Online.

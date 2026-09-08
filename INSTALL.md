# GTA Spotify Radio

An experimental Story Mode plugin for GTA V Enhanced. It creates a Spotify
Connect receiver named **GTA V Radio** and plays it as a station on the radio
wheel, following the game's own radio behaviour: the pause menu pauses it, it
ducks under dialogue, it follows the Music volume slider, and it is muffled
inside the cabin and from outside the car.

## Install

1. Copy `GtaSpotifyRadio.asi` and `GtaSpotifyRadio.ini` into the GTA V Enhanced
   folder beside `GTA5_Enhanced.exe`.
2. Start GTA V in Story Mode. On first launch the plugin writes a silent
   placeholder track to
   `Documents\Rockstar Games\GTA V Enhanced\User Music`.
3. Open **Settings > Audio** and rescan / enable user music once. Self Radio
   now appears on the radio wheel. This step is only needed the first time.
4. In Spotify, open the device picker and select **GTA V Radio**, then start a
   song or playlist.
5. Get in a vehicle and select **Self Radio** on the radio wheel, or press
   **F7**.

### Taking over a normal station instead

If you would rather replace an existing station than use Self Radio, set this
in `GtaSpotifyRadio.ini`:

```ini
[Radio]
Mode=Replace
Station=RADIO_02_POP
```

While you are tuned to that station the game's own version of it is frozen and
muted, and it is restored when you retune away. If the station stays silent
even with Spotify playing, set `RespectGameRadioFade=0` under `[Mix]` and check
`GtaSpotifyRadio.log`.

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

- **Self Radio never appears on the wheel** — open `GtaSpotifyRadio.log`. It
  lists every Documents root it probed and every folder it found under
  `Rockstar Games`. If it reports no user music folder, find the one GTA
  actually reads and set `UserMusicDir` under `[SelfRadio]` to its full path.
  Remember the one-time rescan in Settings > Audio after the placeholder is
  written. If Self Radio does not exist in your build at all, switch to
  `Mode=Replace`.
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

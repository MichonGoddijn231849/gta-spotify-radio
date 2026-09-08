# GTA Spotify Radio

An experimental GTA V Enhanced Spotify Connect radio station for Story Mode.

The plugin advertises a Spotify Connect device named **GTA V Radio**, decodes
Spotify Premium audio to PCM, and renders it from inside the GTA process as a
station on the radio wheel. It does not stop at "play audio over the game": the
output stage follows the game's own radio state so the station behaves like a
native one.

## What it emulates

The plugin cannot inject into RAGE's proprietary radio mixer, so instead it
reads the game every frame through ScriptHookV and drives its own XAudio2 voice
to match:

| Native radio behaviour | How it is reproduced |
| --- | --- |
| Volume matches the other stations | Spotify loudness normalisation plus a `VolumeDb` trim |
| Music volume slider | `GET_MUSIC_VOL_SLIDER`, followed live |
| Pause menu pauses the music | `IS_PAUSE_MENU_ACTIVE`, plus a heartbeat watchdog for loading screens and alt-tab |
| Cutscenes, character switches, screen fades | `IS_CUTSCENE_PLAYING`, `IS_PLAYER_SWITCH_IN_PROGRESS`, `IS_SCREEN_FADED_OUT` |
| Mission mutes and the retune sweep | `IS_RADIO_FADED_OUT`, `IS_RADIO_RETUNING` |
| Quieter while characters talk | `IS_SCRIPTED_CONVERSATION_ONGOING`, `IS_MOBILE_PHONE_CALL_ONGOING`, ambient speech |
| Muffled inside a closed cabin | Low-pass filter, bypassed on bikes, boats and roof-down convertibles |
| Distant and muffled from outside the car | Distance rolloff against the gameplay camera plus a heavier low-pass |
| Underwater muffling | `GET_ENTITY_SUBMERGED_LEVEL` |
| Radio off with the engine, on the wheel, on the phone | `IS_VEHICLE_RADIO_ON`, `GET_IS_VEHICLE_ENGINE_RUNNING`, `IS_MOBILE_PHONE_RADIO_ACTIVE` |

Pausing is a real pause: the XAudio2 voice stops without discarding queued
audio and the Spotify stream is held at an audio boundary, so a track resumes
exactly where it stopped instead of skipping ahead.

## Getting a slot on the radio wheel

GTA V Enhanced does not let a plugin add a station to the wheel without RPF
audio-metadata modding, so the plugin takes one of two routes, chosen with
`Mode` in `GtaSpotifyRadio.ini`:

- **`SelfRadio` (default)** — rides on Self Radio (`RADIO_19_USER`). The plugin
  writes a long silent MP3 into your GTA user music folder, which is what makes
  Self Radio appear on the wheel and gives it something inaudible to "play".
  The native station, its wheel entry and the whole radio state machine are
  real; only the audio comes from this plugin. Rescan user music once in
  Settings > Audio after first launch.
- **`Replace`** — takes over an existing station such as `RADIO_02_POP`. While
  you are tuned to it the native station is frozen and muted through an audio
  scene, and it is restored when you retune away.

## Controls

- F7: tune to the Spotify station, or back to the previous one
- F9: pause/resume
- F10: next queued track
- F8: previous track

Keys, volume, ducking, muffling and fade times are all configurable in
`GtaSpotifyRadio.ini`.

## Build

Requirements:

- Windows and Visual Studio 2022 with **Desktop development with C++**
- CMake
- A Spotify Premium account for playback

Clone with submodules and run:

```powershell
git clone --recurse-submodules https://github.com/MichonGoddijn231849/gta-spotify-radio.git
cd gta-spotify-radio
.\build.cmd
```

The ASI is written to `build\GtaSpotifyRadio.asi`. The project statically links
the MIT-licensed `librespotclib` source and dynamically uses ScriptHookV at
runtime.

## Install

See [INSTALL.md](INSTALL.md). A tested binary package is also available from
the repository's Releases page.

## Safety

Story Mode only. Do not try to load ASI plugins in GTA Online.

## Status

The audio still leaves the game through its own XAudio2 voice rather than
RAGE's radio submix, so the emulation above is exactly that: emulation. It
matches the native stations closely enough to be hard to pick out in normal
play, but reverb from interiors and tunnels, and the mixer's own compression,
are approximated rather than inherited. The station name and wheel icon come
from whichever native station is hosting it.

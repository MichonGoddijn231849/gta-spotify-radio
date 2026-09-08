# GTA Spotify Radio

An experimental GTA V Enhanced Spotify Connect radio for Story Mode.

The plugin advertises a Spotify Connect device named **GTA V Radio**, decodes
Spotify Premium audio to PCM, and renders that audio from inside the GTA
process. F7 toggles a virtual Spotify station from inside any vehicle and
silences the native station while Spotify is active. If GTA exposes Self
Radio, selecting it also activates Spotify automatically.

## Prototype controls

- F7: toggle the virtual Spotify station
- F9: pause
- F10: next queued track
- F8: previous track

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

The ASI is written to `build\GtaSpotifyRadio.asi`. The project
statically links the MIT-licensed `librespotclib` source and dynamically uses
ScriptHookV at runtime.

## Install

See [INSTALL.md](INSTALL.md). A tested binary package is also available from
the repository's Releases page.

## Safety

Story Mode only. Do not try to load ASI plugins in GTA Online.

## Status

This is an initial integration prototype. Audio is rendered through XAudio2
inside the GTA process. It does not yet inject into the proprietary RAGE radio
mixer, so native cabin/tunnel radio DSP and a named radio-wheel icon are later
milestones.

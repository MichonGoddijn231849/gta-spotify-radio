# Changelog

The section for a version becomes the body of its GitHub release, so write it
for someone reading the release page.

## v0.2.0

Spotify now behaves like a native GTA radio station rather than audio played
over the top of the game.

### What changed

The plugin stands in for an existing radio station (`RADIO_02_POP` by default)
and drives its own XAudio2 voice from the game's radio state, read every frame:

- The pause menu pauses it, and it resumes on the same beat rather than
  skipping ahead. A watchdog covers loading screens and alt-tab.
- It ducks under scripted conversations, phone calls and ambient speech.
- It follows the in-game Music volume slider.
- It is muffled inside a closed cabin, distant and muffled from outside the
  car, and muffled again underwater. Bikes, boats and roof-down convertibles
  get the full-range signal.
- It follows cutscenes, character switches, screen fades, mission mutes and the
  retune sweep, and stops with the engine.

### Worth knowing

The radio wheel reads OFF while Spotify plays. No native mutes a station while
it stays selected, so silencing the station being stood in for means switching
the radio off. The wheel itself still works normally — the plugin lets go of
the radio while you are retuning.

Volume is a by-ear calibration against the native stations. Page Up and Page
Down trim it live and report the level; write the value you settle on into
`VolumeDb`.

Self Radio does not work on GTA V Enhanced. The game never indexes user music,
so the station stays off the wheel even with ordinary MP3s present.
`Mode=SelfRadio` is kept for Legacy only.

### Install

Copy `GtaSpotifyRadio.asi` and `GtaSpotifyRadio.ini` next to
`GTA5_Enhanced.exe`, pick **GTA V Radio** in Spotify's device list, then tune to
Pop in a vehicle. Story Mode only, and it needs ScriptHookV for Enhanced. See
INSTALL.md.

## v0.1.0

First integration prototype. Advertises a Spotify Connect device named **GTA V
Radio**, decodes Spotify Premium audio and plays it through its own XAudio2
voice inside the GTA process, toggled with a hotkey.

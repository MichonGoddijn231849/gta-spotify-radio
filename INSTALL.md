# GTA Spotify Radio prototype

This is an experimental Story Mode plugin for GTA V Enhanced. It creates a
Spotify Connect receiver named **GTA V Radio**, decodes Spotify Premium audio,
and plays it through XAudio2 from inside the GTA process.

## Install

1. Copy `GtaSpotifyRadio.asi` and `GtaSpotifyRadio.ini` into the GTA V Enhanced
   folder beside `GTA5_Enhanced.exe`.
2. Start GTA V in Story Mode.
3. In Spotify, open the device picker and select **GTA V Radio**.
4. Start a song or playlist in Spotify.
5. Enter a vehicle and press **F7**. GTA's native station switches off and the
   Spotify stream becomes active. Press F7 again to restore the prior station.

Self Radio is optional. If it already exists on the radio wheel, selecting it
also activates Spotify, but no music scan or placeholder files are required.

The first Spotify pairing is cached locally in
`GtaSpotifyRadio\cache\credentials.dat`. Treat this file as private account
data.

## Controls

- F7: toggle the virtual Spotify station
- F9: pause/resume
- F10: next track in the received Spotify context
- F8: previous track

Windows virtual-key codes and output volume are configurable in
`GtaSpotifyRadio.ini`.

## Current limitation

The prototype owns an XAudio2 voice inside the GTA process, but does not yet
hook the proprietary RAGE radio mixer. It therefore does not yet inherit
GTA's cabin/tunnel equalization, respond to the in-game Music Volume slider,
or add a named icon to the radio wheel.

## Safety

Use only in Story Mode with BattlEye disabled. Do not use ASI plugins in GTA
Online.

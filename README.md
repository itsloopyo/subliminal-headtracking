# Subliminal Head Tracking

![Subliminal running with this mod](https://raw.githubusercontent.com/itsloopyo/subliminal-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Subliminal that moves the view with your
head while your mouse or controller keeps control of look and interaction,
driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR
headset required.

## Features

- **Decoupled look and interaction** - your head moves the view, the mouse or
  controller still points at what you interact with, and the crosshair ring
  follows that point.
- **6DOF tracking** - turn, tilt and lean, with the lean clamped so the view
  stays inside the player.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Subliminal](https://store.steampowered.com/app/2300840/Subliminal/) on Steam.
- A head tracking source that can send the OpenTrack UDP protocol:
  [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a VR
  headset, or a phone app that sends it directly.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Subliminal**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the
   [Releases](https://github.com/itsloopyo/subliminal-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your copy of the game, point it at the install
folder either way round:

```powershell
# Environment variable
$env:SUBLIMINAL_PATH = "D:\Games\Subliminal"
.\install.cmd

# Or as an argument
.\install.cmd "D:\Games\Subliminal"
```

### Manual Installation

The payload goes next to the shipping executable, in
`Subliminal\Binaries\Win64\` under the game folder. No mod manager deploys this
mod: Vortex and Mod Organizer place files into one fixed subtree per game, and
this one has to land beside the exe, which is why there is a single installer
ZIP and no Nexus page.

From the extracted ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into
   `Subliminal\Binaries\Win64\` and rename it to `winmm.dll`. That is the import
   the shipping exe already has, so it is the proxy name the loader has to use
   here.
2. Copy `plugins\SubliminalHeadTracking.asi` into the same folder.

`HeadTracking.ini` and `HeadTracking.log` are written to that folder on first
launch.

## Setting Up OpenTrack

1. Open OpenTrack.
2. Set **Output** to `UDP over network`.
3. Open its options and set the address to `127.0.0.1` and the port to `4242`.
4. Pick an **Input**, then press **Start**.

Centering is done in the tracker: OpenTrack's Center bind, the CENTER button in
a phone app, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR and let it pick the headset up.
3. In OpenTrack, set **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, address `127.0.0.1`, port `4242`.

### Webcam Setup

In OpenTrack, set **Input** to the `neuralnet tracker`. It runs off an ordinary
webcam and needs no markers, clips or IR hardware. Leave **Output** on
`UDP over network`, address `127.0.0.1`, port `4242`, then press **Start**.

### Phone App Setup

The mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone
app is usable here if it sends that protocol itself, or ships a PC-side
companion that does. Check your app against that first.

For an app that does send it, what decides the wiring is how much filtering the
app does before the packet leaves the phone. An app that filters on-device can
point straight at this PC's LAN address on port `4242`. A raw or lightly
filtered feed sent direct will jitter, because the mod's smoothing is sized to
take the edge off a clean signal rather than to rescue a noisy one, and that app
should go through OpenTrack instead so its filters and curves can clean the feed
up first.

The test is quick: send direct, hold your head still, and if the view drifts or
shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody
with a phone already in their pocket. It filters on-device, so it can send
directly. Any app that filters enough noise works the same way.

A tracker reaching the game over the network gets `RemoteSmoothing` (0.15 by
default) rather than `LocalSmoothing` (0.0), because the classifier sees a
transport rather than a machine: only a loopback address (`127.0.0.0/8`,
or `::1`) counts as local.
Running OpenTrack on this same PC but sending to the machine's LAN address
instead of `127.0.0.1` is therefore treated as remote and gets the heavier
value.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

`HeadTracking.ini` is written next to the game executable, in
`Subliminal\Binaries\Win64\`, the first time the mod runs. Edit it and restart
the game to apply.

```ini
[Network]
UdpPort=4242

[General]
EnableOnStartup=1
; Yaw mode: 1 = horizon-locked yaw (default), 0 = camera-local yaw.
; Page Down (or Ctrl+Shift+H) toggles it in game.
WorldSpaceYaw=1

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.
; The value is picked per connection from the packet source address:
; LocalSmoothing for a tracker sending from this PC over loopback
; (127.0.0.1), RemoteSmoothing for anything else - including a tracker on
; this same PC that sends to the machine's LAN address instead.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
LimitX=0.30
LimitY=0.20
LimitYDown=0.20
LimitZ=0.40
LimitZBack=0.10

[Collision]
; Stop a lean putting your eye inside a wall. The mod sweeps the game's
; own collision from where the camera really is towards where your head
; asks it to go, and cuts the lean to whatever the room leaves.
CollisionEnabled=1
; How far off a surface the eye is held, in centimetres.
CollisionRadius=20
; Which trace channel level geometry blocks (ETraceTypeQuery index).
CollisionChannel=0
; How quickly the lean opens back up once the wall clears. 0.9 is about
; a fifth of a second; tightening is always instant.
CollisionReleaseSmoothing=0.90

[Reticle]
; Move the game's crosshair ring to where you are actually pointing.
; Head tracking moves the view but not the aim, so without this the ring
; sits at the centre of the picture and stops marking the thing you
; would interact with.
Enabled=1
; How far the aim trace reaches, in centimetres.
TraceDistance=20000
; Which trace channel the aim ray runs on (ETraceTypeQuery index).
TraceChannel=0

[Flashlight]
; Point the flashlight where you are looking. The game hangs the beam
; off the mouse aim, so without this it keeps lighting whatever the
; mouse points at while you look somewhere else.
Enabled=1
; How far the beam turns relative to your head. 1.5 leads the view,
; 1.0 matches it, 0 pins the beam back on the mouse aim.
Multiplier=1.5

[Hotkeys]
; Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same
; job and is not configurable.
YawModeKey=0x22

[Dev]
; Ctrl+Shift+U / Ctrl+Shift+J cycle which GetPlayerViewPoint caller is
; head-tracked. Only needed to re-confirm the render caller after a
; game patch moves it.
InjectHotkeys=0
; List the live UMG widgets the reticle pass could move, to HeadTracking.log.
WidgetDump=0
```

`[Dev]` is for re-confirming the camera hook after a game patch. Leave both at
0 unless you are asked to change them; `InjectHotkeys=1` adds `Ctrl+Shift+U`
and `Ctrl+Shift+J` to the bindings above.

`CollisionRadius` must be at least 11, one centimetre clear of the engine's 10cm
near clip distance. A smaller value is clamped up to 11 and the log says so. A
standoff inside the near plane stops the view short of the wall and the wall is
culled anyway, which is the same complaint with extra steps.

## Troubleshooting

**Mod not loading:**

- Check `HeadTracking.log` next to `Subliminal-Win64-Shipping.exe`. Its first
  lines say whether the mod matched your game build and whether the camera hook
  was installed. If it says the build is newer than any this mod knows about,
  the game has been patched and the mod needs an update - it stays dormant
  rather than hooking a build it does not recognize.
- If there is no log at all, the loader is not being picked up. Check that
  `winmm.dll` is in `Subliminal\Binaries\Win64\` alongside the `.asi`.

**No tracking response:**

- The heartbeat line in the log carries `udpData=none` when no tracker has ever
  sent, and `udpData=stale` when one sent and stopped. Check the port matches
  the one your tracker is sending to.
- `udpPort=waiting-for-port` means something else still holds the port - usually
  a previous run of the game that has not fully exited. That one clears itself.
- A tracker on another device has to send to this PC's LAN address rather than
  `127.0.0.1`, and Windows Firewall has to allow the game to receive on UDP
  4242.
- Press `End` (or `Ctrl+Shift+Y`) once in case tracking was toggled off.

**Jittery or unstable tracking:**

- Raise `RemoteSmoothing` for a tracker coming in over the network, or
  `LocalSmoothing` for one sending over loopback. Both run 0.0 to 1.0 and cover
  rotation and position.
- A phone app sending a raw feed direct is the usual cause. Route it through
  OpenTrack so its filters and curves clean the feed up first, or turn the app's
  own filtering up.
- A webcam tracker in a dim room produces a noisy pose. More light on your face
  does more than any smoothing value.

**Wrong rotation axis:**

- `InvertYaw`, `InvertPitch` and `InvertRoll` in `[Rotation]` flip an axis that
  moves the wrong way.
- Yaw that feels wrong only when looking hard up or down is the yaw mode rather
  than a sign. Toggle it with `Page Down` (or `Ctrl+Shift+H`). World-locked, the
  default, keeps the horizon level, so looking at the floor and turning your
  head pans across it. Camera-local turns about the camera's own up-axis, which
  leans the horizon instead.

**Leaning puts the view through a wall:**

- `[Collision] CollisionEnabled=1` sweeps the game's collision and cuts the lean
  to what the room leaves. `CollisionRadius` is how far off a surface the view is
  held, in centimetres.

**The view stops following my head in menus:**

- By design. Tracking is suppressed whenever a menu, a pause or a loading screen
  has control, and resumes when you do. The log's heartbeat line reads
  `gameplay=NO` while it is stood down.

## Updating

Download the new release and run `install.cmd` again. It overwrites the payload
and leaves `HeadTracking.ini` alone, so your settings survive.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's files. The ASI loader is only
removed if the installer put it there; use `uninstall.cmd /force` to remove it
anyway.

## Building from Source

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/subliminal-headtracking.git
cd subliminal-headtracking
pixi run test      # behaviour locks - no game needed
pixi run package   # installer ZIP in release/
```

Visual Studio with the C++ toolchain, CMake 3.20+ and git are the
prerequisites. The configure step clones MinHook from GitHub, so the first
build needs a network connection. Nothing in the build reads the game install.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) covers everything bundled with
or compiled into the payload.

## Credits

- Subliminal is made by Accidental Studios, LLC.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by
  ThirteenAG, vendored as the mod loader.
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, compiled in
  for the camera hook.
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared
  head tracking library behind this mod.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Accidental
Studios, LLC. Use at your own risk.

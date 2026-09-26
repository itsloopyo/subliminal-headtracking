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

`CameraUnlock.ini`, the mod's settings file, and `HeadTracking.log` are written
to that folder on first launch.

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

Two equivalent binding sets - use whichever your keyboard has. Both are the
defaults of the key lists in `CameraUnlock.ini` (see Configuration), where each
action can be given other keys:

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

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment
you change them, so the game starts in them next time. Toggling tracking with
`End` lasts for the session only; whether tracking starts on is
`EnableOnStartup`.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `Subliminal\Binaries\Win64\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`
- `LightFollowsHead=true`
- `LightMultiplier=1.5`

With every setting at its default, the file reads:

```ini
; Subliminal head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=default
; How far, in centimetres, the view is held off a wall when you lean into it.
; Keep it above the camera's near clip distance, or the wall is not drawn anyway.
CollisionMargin=20.0
; Which of the game's collision channels the wall check tests against, 0 to 31.
; Any other number uses channel 0.
; CollisionChannel=0
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=default
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=default

[Aim]
; How far, in centimetres, the aim trace reaches, 100 to 1000000. The trace finds
; the point you would interact with, so the crosshair ring can sit on it.
AimTraceDistance=20000.0
; Which of the game's collision channels the aim trace tests against, 0 to 31.
; AimTraceChannel=0

[Dev]
; For development. Steps which of the game's view point callers is given the head
; pose, to find the render path again after a game patch.
InjectNextKey=
; For development. Steps the other way.
InjectPreviousKey=
; For development. true: write the game's crosshair and prompt widgets to
; HeadTracking.log, to find them again after a game patch.
WidgetDump=false
```
<!-- /cameraunlock:config -->

### Walls

`CollisionEnabled` sweeps the game's own collision from where the camera really
is towards where your head asks it to go, and cuts the lean to whatever the room
leaves. `CollisionMargin` is how far off a surface the view is held, in
centimetres. Keep it above the engine's 10cm near clip distance: a standoff
inside the near plane stops the view short of the wall and the wall is culled
anyway. `CollisionChannel` is the collision channel the sweep tests against, 0 to
31; a number outside that range uses channel 0 and the log says so.

### The crosshair

The game's crosshair ring always follows the point the mouse or controller is
aiming at, so it keeps marking what you would interact with while your head
moves the view. There is no setting to turn that off. Under `[Aim]`,
`AimTraceDistance` is how far, in centimetres, the aim trace reaches, and
`AimTraceChannel` the collision channel it tests against.

### The flashlight

`LightFollowsHead` points the flashlight where you are looking rather than where
the mouse aims. `LightMultiplier` scales the head pose the beam is given. The
default, 1.5, leads the view, because turning your head puts your eyes off the
centre of the screen and a beam matched to the view lands short of what you are
looking at. 1.0 moves the beam with the view, and 0 leaves it where the game
aimed it.

### Development settings

`[Dev]` is for re-confirming the camera hook after a game patch. Leave it as it
is unless you are asked to change it. `InjectNextKey` and `InjectPreviousKey`
are unbound by default. An earlier version bound `Ctrl+Shift+U` and
`Ctrl+Shift+J` to them with `InjectHotkeys=1`, and the import carries that over.

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
  `LocalSmoothing` for one sending over loopback. Both are under `[Smoothing]`
  in `CameraUnlock.ini`, run 0.0 to 1.0 and cover rotation and position.
- A phone app sending a raw feed direct is the usual cause. Route it through
  OpenTrack so its filters and curves clean the feed up first, or turn the app's
  own filtering up.
- A webcam tracker in a dim room produces a noisy pose. More light on your face
  does more than any smoothing value.

**Wrong rotation axis:**

- The mod applies the pose as your tracker sends it. If an axis moves the wrong
  way, invert it in your tracker.
- Yaw that feels wrong only when looking hard up or down is the yaw mode rather
  than a sign. Toggle it with `Page Down` (or `Ctrl+Shift+H`). World-locked, the
  default, keeps the horizon level, so looking at the floor and turning your
  head pans across it. Camera-local turns about the camera's own up-axis, which
  leans the horizon instead.

**Leaning puts the view through a wall:**

- `CollisionEnabled` under `[Position]` sweeps the game's collision and cuts the
  lean to what the room leaves. `CollisionMargin` is how far off a surface the
  view is held, in centimetres. See Walls under Configuration.

**The view stops following my head in menus:**

- By design. Tracking is suppressed whenever a menu, a pause or a loading screen
  has control, and resumes when you do. The log's heartbeat line reads
  `gameplay=NO` while it is stood down.

## Updating

Download the new release and run `install.cmd` again. It overwrites the payload
and leaves `CameraUnlock.ini` alone, so your settings survive. Updating from
0.1.0 moves your settings from `HeadTracking.ini` into `CameraUnlock.ini` at the
first start; see Configuration.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's files. `CameraUnlock.ini`, and a
`HeadTracking.ini` an earlier version left, stay in place so a reinstall keeps
your settings. The ASI loader is only removed if the installer put it there; use
`uninstall.cmd /force` to remove it anyway.

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

# Changelog

## [Unreleased]

### Added

- The tracking mode (`PageUp` / `Ctrl+Shift+G`) and the yaw mode (`PageDown` / `Ctrl+Shift+H`) are saved to `CameraUnlock.ini` the moment you change them, and the game starts in them next time. `End` still turns head tracking on or off for the current session only; `EnableOnStartup` says whether it is on when the game starts.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `Subliminal\Binaries\Win64\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity or axis inversion you changed from its default. Set these in your tracker instead.
  - `[Reticle] Enabled=0`. The crosshair ring now always follows the aim.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `End`, `PageUp` and the three chords were fixed before and can now be changed or removed like any other key. `[Hotkeys] YawModeKey=0x22` becomes `YawModeKey=PageDown, Ctrl+Shift+H`.
- Settings keep their values under their new names: `[Rotation] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`; the `[Position]` limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`; `[Position] Enabled=0` becomes the tracking mode `RotationEnabled=true` and `PositionEnabled=false`; `[Collision] CollisionEnabled`, `CollisionChannel` and `CollisionReleaseSmoothing` move to `[Position]`, and `CollisionRadius` becomes `[Position] CollisionMargin`; `[Reticle] TraceDistance` and `TraceChannel` are `[Aim] AimTraceDistance` and `AimTraceChannel`; `[Flashlight] Enabled` and `Multiplier` are `[Light] LightFollowsHead` and `LightMultiplier`. On and off settings are written `true` and `false`.
- `[Dev] InjectHotkeys=1` becomes `InjectNextKey=Ctrl+Shift+U` and `InjectPreviousKey=Ctrl+Shift+J`, which are empty, and bind nothing, by default.
- A value in `CameraUnlock.ini` outside a setting's range is not used: it keeps the default, and `HeadTracking.log` names the line. Earlier versions clamped a number outside its range to the nearest bound. `UdpPort` takes 1 to 65535, the smoothing values and `CollisionReleaseSmoothing` 0 to 1, the position limits 0 to 10, `LightMultiplier` 0 to 5, `AimTraceDistance` 100 to 1000000 and `AimTraceChannel` 0 to 31. `CollisionMargin` takes any number from 0 up, and `CollisionChannel` any whole number, where a number outside 0 to 31 uses channel 0 and the log says so. A value `HeadTracking.ini` held is imported as the earlier versions read it.
- `uninstall.cmd` leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so your settings survive a reinstall. It deleted `HeadTracking.ini` before.

### Removed

- The sensitivity and axis inversion settings: `[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Position] SensitivityX`, `SensitivityY` and `SensitivityZ`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.
- `[Reticle] Enabled`, the switch that left the crosshair ring at the centre of the picture. The ring always follows the aim.
- `[Dev] InjectHotkeys`, replaced by `InjectNextKey` and `InjectPreviousKey`.

## [0.1.0] - 2026-09-20

### Other

- Hello world

All notable changes to this project are documented here.

## [0.0.0] - 2026-09-06

### Added
- Initial release.
- Added head tracking that moves the view while the mouse or controller keeps control of look and interaction, driven by any OpenTrack compatible tracker over UDP.
- Added 6DOF tracking, with the lean clamped to a range that keeps the view inside the player.
- Added crosshair compensation so the ring stays on the point you would interact with while the head moves the view.
- Added hotkeys to toggle tracking, cycle the tracking mode and switch between world and camera local yaw, on the nav cluster keys and on Ctrl+Shift chords.

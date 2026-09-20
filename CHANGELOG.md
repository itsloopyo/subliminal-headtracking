# Changelog

All notable changes to this project are documented here.

## [0.0.0] - 2026-09-06

### Added
- Initial release.
- Added head tracking that moves the view while the mouse or controller keeps control of look and interaction, driven by any OpenTrack compatible tracker over UDP.
- Added 6DOF tracking, with the lean clamped to a range that keeps the view inside the player.
- Added crosshair compensation so the ring stays on the point you would interact with while the head moves the view.
- Added hotkeys to toggle tracking, cycle the tracking mode and switch between world and camera local yaw, on the nav cluster keys and on Ctrl+Shift chords.

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace subliminal_ht::widget_probe {

// Log the live UMG objects whose name or class looks like a reticle, crosshair
// or interaction prompt, with their outer chain. This is how the widgets to
// move get identified - their names live in cooked Blueprint assets, so they
// cannot be read out of the EXE. Called on a timer while [Dev] WidgetDump is
// set, so that one of the passes lands while a prompt is on screen.
void DumpCandidates();

}  // namespace subliminal_ht::widget_probe

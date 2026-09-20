// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace subliminal_ht::reticle_mover {

// Push the projected clean-aim offset onto the game's own crosshair ring, so it
// sits where the interaction ray points instead of at the centre of the
// head-tracked picture. The interaction prompts are left where the game puts
// them - see the .cpp for why. Called from the GetPlayerViewPoint hook on the
// render caller, which is a game-thread context - the UObject table and the
// script VM both require that.
void Tick();

}  // namespace subliminal_ht::reticle_mover

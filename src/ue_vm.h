// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

// Calling into the game's script VM, and reading the object graph around it.
//
// Four features need this - the reticle mover, the aim trace, the lean clamp
// and the session gate - and all four do it the same way: resolve
// UObject::ProcessEvent from the active build profile once, then dispatch a
// UFUNCTION through it behind a fault guard. Every caller must already be on
// the game thread.
namespace subliminal_ht::ue_vm {

// Resolve UObject::ProcessEvent from the active build profile, once. False when
// the profile carries no RVA for it, in which case nothing else here can run.
bool Ready();

// The RVA Ready() resolved, for the caller that wants to say so in the log.
std::uintptr_t ProcessEventRva();

// Dispatch a UFUNCTION. Returns false if the call faulted - the object stopped
// being what the caller thought it was between its liveness test and here - so
// the caller can drop it and go looking for its replacement rather than pushing
// into a dead object forever.
bool Dispatch(void* self, void* function, void* params);

// The chain of outer names above `obj`, at most `depth` links, each one
// prefixed with `separator`. Stops early at the first outer that will not read.
std::string OuterChain(std::uintptr_t obj, int depth, const char* separator);

// Rate limit for a resolve that has not succeeded yet.
//
// Every lookup here goes through FindLiveObject, which walks the whole
// GUObjectArray and builds a std::string for each live object it passes -
// hundreds of thousands of them in a UE5 shipping build. A feature whose
// objects are not visible yet must therefore NOT retry on every frame, or the
// render thread stalls for as long as they stay invisible.
//
// One instance per feature rather than one shared clock, so a probe that has
// just retried does not block another feature's first attempt. The objects
// appear at engine init, and a quarter second is invisible against a level
// load.
// Retrying forever is not free and not silent-safe. Each attempt walks the whole
// object table, so a feature whose objects never appear spends that walk every
// interval for the rest of the session, on the render thread; and a feature that
// never resolves never reaches the log line that says so, because those are
// written on the give-up path. The objects appear at engine init, so a feature
// still unresolved after this many attempts is not waiting for them.
class ResolveRetry {
public:
    // True at most once per interval, and never once the attempts are spent. On
    // the frames in between an unresolved feature costs one clock read.
    bool Due();

    // True once Due() has handed out kMaxAttempts and none of them resolved.
    // The caller reports this once and stops asking.
    bool Exhausted() const { return m_attempts >= kMaxAttempts; }

private:
    static constexpr std::uint64_t kIntervalMs = 250;
    // 10 seconds at the interval above, which covers a level load on a slow
    // disk. This is the ONLY attempt budget: a feature that kept a second
    // counter of its own would stop being called once this one ran out and
    // would never reach its own give-up, so its diagnostic would never print.
    static constexpr int kMaxAttempts = 40;
    std::uint64_t m_lastAttemptMs = 0;
    int m_attempts = 0;
};

}  // namespace subliminal_ht::ue_vm

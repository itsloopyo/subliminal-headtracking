// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"

#include <array>

#include <cameraunlock/memory/pe_fingerprint.h>

#include "logging.h"

namespace subliminal_ht::builds {

// One extern per known build. Never-delete policy: when a game patch breaks the
// current build, derive new RVAs and ADD a new profile here (newest at the top
// of kKnownProfiles) without removing the old one. Users on the un-patched build
// still match their old profile by PE fingerprint.
extern const BuildProfile kSteamProfile_20260906;

namespace {

// Newest-first. The first entry is the "primary" used to label newer/older when
// no profile matches.
constexpr std::array<const BuildProfile*, 1> kKnownProfiles = {
    &kSteamProfile_20260906,
};

const BuildProfile* g_active = nullptr;

// A profile is "complete" iff the camera hook can be installed from it. Lets a
// profile with the correct fingerprint but offsets still TBD register without
// risking activation against zero addresses; the features that need the object
// table check their own globals and stand themselves down, so the camera is the
// only hard requirement.
bool ProfileIsComplete(const BuildProfile* p) {
    return p && p->Offsets.kGetPlayerViewPointRva != 0;
}

}  // namespace

MatchResult SelectProfile(HMODULE host) {
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
        Log::Line("build-check: failed to read PE header from host module");
        return MatchResult::ReadFailed;
    }

    Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
        running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    for (const BuildProfile* p : kKnownProfiles) {
        const bool complete = ProfileIsComplete(p);
        Log::Line("build-check: profile=%s ts=0x%08x size=0x%08x csum=0x%08x%s",
            p->Name, p->Fingerprint.TimeDateStamp,
            p->Fingerprint.SizeOfImage, p->Fingerprint.CheckSum,
            complete ? "" : " (incomplete - offsets TBD)");
        if (running.Matches(p->Fingerprint)) {
            if (!complete) {
                Log::Line("build-check: fingerprint matches %s but its offsets "
                          "are not yet derived - staying dormant", p->Name);
                return MatchResult::HostDiffers;
            }
            g_active = p;
            Log::Line("build-check: matched profile %s", p->Name);
            return MatchResult::Matched;
        }
    }

    // No match. Classify against the primary profile so the log explains
    // direction ("patched newer", "older", or "tampered").
    switch (cameraunlock::memory::ClassifyMismatch(
                running, kKnownProfiles.front()->Fingerprint)) {
        case cameraunlock::memory::FingerprintMismatch::Newer:
            return MatchResult::HostNewer;
        case cameraunlock::memory::FingerprintMismatch::Older:
            return MatchResult::HostOlder;
        case cameraunlock::memory::FingerprintMismatch::Differs:
        default:
            return MatchResult::HostDiffers;
    }
}

const BuildProfile& ActiveProfile() { return *g_active; }

}  // namespace subliminal_ht::builds

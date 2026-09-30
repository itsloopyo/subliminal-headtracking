// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <string>

namespace subliminal_ht::builds {

struct ImageView {
    const std::uint8_t* data;
    std::size_t size;
    std::uintptr_t base;
};

bool ValidateController(std::uintptr_t controller);

bool DiscoverOffsets(ImageView image, OffsetTable& offsets, std::string& reason, std::uint32_t& viewSlot);

}

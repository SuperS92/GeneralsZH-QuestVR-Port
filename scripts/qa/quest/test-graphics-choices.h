// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "VRDevice/QuestGraphicsChoices.h"
static void checkGraphicsChoices() {
    using namespace QuestXR;
    for (uint32_t mode = 0; mode < 5; ++mode) for (uint32_t value = 0; value < 32; ++value)
        require(MapMipFilter(value, mode) == (value == 1 && (mode == 1 || mode == 2) ? 2 : value),
            "only opt-in POINT mip filtering changes; NONE preserved");
    uint32_t w, h;
    require(ChooseEyeExtent(1680, 1760, 1000, 4096, 4096, w, h) && w == 1680 && h == 1760,
        "original extent exact");
    require(ChooseEyeExtent(1680, 1760, 1250, 4096, 4096, w, h) && w == 2100 && h == 2200,
        "1.25 extent exact");
    require(ChooseEyeExtent(1680, 1760, 1500, 4096, 4096, w, h) && w == 2520 && h == 2640,
        "1.5 extent exact");
    require(ChooseEyeExtent(1680, 1760, 9999, 4096, 4096, w, h) && w == 1680 && h == 1760,
        "unknown scale keeps original");
    require(ChooseEyeExtent(4000, 2000, 1500, 5000, 5000, w, h) && w == 4096 && h == 2048,
        "isotropic 4096 diagnostic/image bound");
    require(ChooseEyeExtent(1680, 1760, 1500, 2200, 2200, w, h) && w == 2100 && h == 2200,
        "tighter eye-axis limit preserves scale/aspect");
    require(!ChooseEyeExtent(0, 1760, 1500, 4096, 4096, w, h) && !w && !h, "empty eye extent rejected");
    require(!ChooseEyeExtent(1680, 1760, 1500, 1000, 1000, w, h) && !w && !h,
        "incompatible original eye limits rejected");
    require(!ChooseEyeExtent(0xffffffffu, 1760, 1500, 0xffffffffu, 0xffffffffu, w, h), "overflow-sized extent rejected");
    std::puts("PASS: mip choice identity/NONE and eye scales, isotropic limits, invalid/overflow extents");
}

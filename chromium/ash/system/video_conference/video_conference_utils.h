// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_VIDEO_CONFERENCE_VIDEO_CONFERENCE_UTILS_H_
#define ASH_SYSTEM_VIDEO_CONFERENCE_VIDEO_CONFERENCE_UTILS_H_

#include <string>

namespace ash {

enum class VcEffectId;

namespace video_conference_utils {

// Get the histogram name for the click histogram associated with `effect_id`.
std::string GetEffectHistogramNameForClick(VcEffectId effect_id);

// Get the histogram name for the initial state histogram associated with
// `effect_id`.
std::string GetEffectHistogramNameForInitialState(VcEffectId effect_id);

}  // namespace video_conference_utils

}  // namespace ash

#endif  // ASH_SYSTEM_VIDEO_CONFERENCE_VIDEO_CONFERENCE_UTILS_H_
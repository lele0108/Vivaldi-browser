/*
 * Copyright (C) 2008, 2009, 2010 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_THEME_TYPES_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_THEME_TYPES_H_

namespace blink {

// Must follow css_value_keywords.json5 order
// kAutoPart is never returned by ComputedStyle::EffectiveAppearance()
enum ControlPart {
  kNoControlPart,
  kAutoPart,
  kCheckboxPart,
  kRadioPart,
  kPushButtonPart,
  kSquareButtonPart,
  kButtonPart,
  kListboxPart,
  kMediaControlPart,
  kMenulistPart,
  kMenulistButtonPart,
  kMeterPart,
  kProgressBarPart,
  kSliderHorizontalPart,
  kSliderVerticalPart,
  kSearchFieldPart,
  kTextFieldPart,
  kTextAreaPart,
  // Order matters when determinating what keyword is valid in the CSSParser.
  // Values after kTextAreaPart are not recognized as appearance values.
  kInnerSpinButtonPart,
  kMediaSliderPart,
  kMediaSliderThumbPart,
  kMediaVolumeSliderPart,
  kMediaVolumeSliderThumbPart,
  kSliderThumbHorizontalPart,
  kSliderThumbVerticalPart,
  kSearchFieldCancelButtonPart,
};

}  // namespace blink
#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_THEME_TYPES_H_

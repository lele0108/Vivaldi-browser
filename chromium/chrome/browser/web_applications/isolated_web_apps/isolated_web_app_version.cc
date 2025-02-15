// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/isolated_web_apps/isolated_web_app_version.h"

#include <array>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/types/expected.h"
#include "base/version.h"

namespace web_app {

// This parser validates that the given version matches `<version core>` in the
// Semantic Versioning specification: https://semver.org.
base::expected<std::array<uint32_t, 3>, IwaVersionParseError>
ParseIwaVersionIntoComponents(base::StringPiece version_string) {
  constexpr size_t NUM_COMPONENTS = 3;
  std::array<uint32_t, NUM_COMPONENTS> components;

  std::vector<base::StringPiece> component_strings = base::SplitStringPiece(
      version_string, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (component_strings.size() != NUM_COMPONENTS) {
    return base::unexpected(IwaVersionParseError::kNotThreeComponents);
  }

  for (size_t component_idx = 0; component_idx < NUM_COMPONENTS;
       ++component_idx) {
    const auto& component_string = component_strings[component_idx];

    if (component_string.empty()) {
      return base::unexpected(IwaVersionParseError::kEmptyComponent);
    }

    // Disallow leading 0s, but allow a single 0.
    if (component_string.size() > 1 && component_string[0] == '0') {
      return base::unexpected(IwaVersionParseError::kLeadingZero);
    }

    // Check that the component only consists of digits.
    if (!base::ranges::all_of(component_string, &base::IsAsciiDigit<char>)) {
      return base::unexpected(IwaVersionParseError::kNonDigit);
    }

    unsigned int number;
    if (!StringToUint(component_string, &number)) {
      return base::unexpected(IwaVersionParseError::kCannotConvertToNumber);
    }
    // StringToUint returns unsigned int but Version fields are uint32_t.
    static_assert(sizeof(uint32_t) == sizeof(unsigned int),
                  "uint32_t must be same as unsigned int");

    components[component_idx] = number;
  }
  return components;
}

std::string IwaVersionParseErrorToString(IwaVersionParseError error) {
  switch (error) {
    case IwaVersionParseError::kNotThreeComponents:
      return "A version must consist of exactly three components separated by "
             "dots (`x.y.z`)";
    case IwaVersionParseError::kEmptyComponent:
      return "A version component may not be empty";
    case IwaVersionParseError::kNonDigit:
      return "A version component may only contain digits";
    case IwaVersionParseError::kLeadingZero:
      return "A version component may not have leading zeros";
    case IwaVersionParseError::kCannotConvertToNumber:
      return "A version component could not be converted into a number";
  }
}

}  // namespace web_app

// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_
#define CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "base/strings/string_piece.h"
#include "chromeos/ash/components/dbus/dlcservice/dlcservice_client.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/views/widget/widget.h"

namespace borealis {

// This is used by the Borealis installer app.
// Generated by crx_file::id_util::GenerateId("org.chromium.borealis");
extern const char kInstallerAppId[];
// This is the id of the main application which borealis runs.
extern const char kClientAppId[];
// This is an id for an app that looks like the steam client, which is shown to
// users in search who want to install it. Generated by:
// crx_file::id_util::GenerateId("org.chromium.borealis.installer.entrypoint").
// See b/243733660 for details.
extern const char kLauncherSearchAppId[];
// App IDs prefixed with this are unidentified and should be largely ignored.
// These are "anonymous" apps, created because a window couldn't be associated
// with any known app. When these appear in Borealis it's usually caused by
// the main app; but we can't rely on that, so just ignore them.
extern const char kIgnoredAppIdPrefix[];
// This is used to install the Borealis DLC component.
extern const char kBorealisDlcName[];
// The regex used for extracting the Borealis app ID of an application.
extern const char kBorealisAppIdRegex[];
// Base64-encoded allowed x-scheme for Borealis apps.
extern const char kAllowedScheme[];
// Error string to replace Proton version info in the event that a GameID
// parsed with /usr/bin/get_compat_tool_versions.py in the Borealis VM does not
// match the GameID expected based on extraction with kBorealisAppIdRegex.
extern const char kCompatToolVersionGameMismatch[];
// Query parameter key for device information in the borealis feedback
// form.
extern const char kDeviceInformationKey[];

struct CompatToolInfo {
  absl::optional<int> game_id;
  std::string proton = "None";
  std::string slr = "None";
};

// Returns true if it's a non game borealis app (e.g. Steam client).
// Note that this does not check if the app is from the Borealis VM.
bool IsNonGameBorealisApp(const std::string& app_id);

// Returns a Borealis app ID parsed from |exec|, or nullopt on failure.
// TODO(b/173547790): This should probably be moved when we've decided
// the details of how/where it will be used.
absl::optional<int> GetBorealisAppId(std::string exec);

// Returns the Borealis app ID of the |window|, or nullopt on failure.
absl::optional<int> GetBorealisAppId(const aura::Window* window);

// Checks that a given URL has the allowed scheme and that its contents starts
// with one of the URLs in the allowlist.
bool IsExternalURLAllowed(const GURL& url);

// Executes /usr/bin/get_compat_tool_versions.py in the borealis VM, which
// outputs the compat tool version information of any recent game session.
bool GetCompatToolInfo(const std::string& owner_id, std::string* output);

// Parses the output returned by GetCompatToolInfo.
CompatToolInfo ParseCompatToolInfo(absl::optional<int> game_id,
                                   const std::string& output);

}  // namespace borealis

#endif  // CHROME_BROWSER_ASH_BOREALIS_BOREALIS_UTIL_H_

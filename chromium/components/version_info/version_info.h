// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VERSION_INFO_VERSION_INFO_H_
#define COMPONENTS_VERSION_INFO_VERSION_INFO_H_

#include <string>

#include "base/notreached.h"
#include "base/sanitizer_buildflags.h"
#include "base/strings/string_piece.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "components/version_info/channel.h"
#include "components/version_info/version_info_values.h"

namespace base {
class Version;
}

namespace version_info {

// Returns the product name and reduced version information for the User-Agent
// header, in the format: Chrome/<major_version>.0.build_version.0, where
// `build_version` is a frozen BUILD number.
const std::string GetProductNameAndVersionForReducedUserAgent(
    const std::string& build_version);

// Returns the product name, e.g. "Chromium" or "Google Chrome".
constexpr base::StringPiece GetProductName() {
  return PRODUCT_NAME;
}

// Returns the version number, e.g. "6.0.490.1".
constexpr std::string GetVersionNumber() {
  return CHROME_PRODUCT_VERSION;
}

// Returns the product name and version information for the User-Agent header,
// in the format: Chrome/<major_version>.<minor_version>.<build>.<patch>.
constexpr base::StringPiece GetProductNameAndVersionForUserAgent() {
  return "Chrome/" PRODUCT_VERSION;
}

// Returns the major component (aka the milestone) of the version as an int,
// e.g. 6 when the version is "6.0.490.1".
int GetMajorVersionNumberAsInt();

// Like GetMajorVersionNumberAsInt(), but returns a string.
std::string GetMajorVersionNumber();

// Returns the result of GetVersionNumber() as a base::Version.
const base::Version& GetVersion();

// Returns a version control specific identifier of this release.
constexpr base::StringPiece GetLastChange() {
  return LAST_CHANGE;
}

// Returns whether this is an "official" release of the current version, i.e.
// whether knowing GetVersionNumber() is enough to completely determine what
// GetLastChange() is.
constexpr bool IsOfficialBuild() {
  return IS_OFFICIAL_BUILD;
}

// Returns the OS type, e.g. "Windows", "Linux", "FreeBSD", ...
constexpr base::StringPiece GetOSType() {
#if BUILDFLAG(IS_WIN)
  return "Windows";
#elif BUILDFLAG(IS_IOS)
  return "iOS";
#elif BUILDFLAG(IS_MAC)
  return "Mac OS X";
#elif BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  return "ChromeOS";
#else
  return "ChromiumOS";
#endif
#elif BUILDFLAG(IS_ANDROID)
  return "Android";
#elif BUILDFLAG(IS_LINUX)
  return "Linux";
#elif BUILDFLAG(IS_FREEBSD)
  return "FreeBSD";
#elif BUILDFLAG(IS_OPENBSD)
  return "OpenBSD";
#elif BUILDFLAG(IS_SOLARIS)
  return "Solaris";
#elif BUILDFLAG(IS_FUCHSIA)
  return "Fuchsia";
#else
  return "Unknown";
#endif
}

// Returns a string equivalent of |channel|, independent of whether the build
// is branded or not and without any additional modifiers.
constexpr base::StringPiece GetChannelString(Channel channel) {
  switch (channel) {
    case Channel::STABLE:
      return "stable";
    case Channel::BETA:
      return "beta";
    case Channel::DEV:
      return "dev";
    case Channel::CANARY:
      return "canary";
    case Channel::UNKNOWN:
      return "unknown";
  }
  NOTREACHED_NORETURN();
}

// Returns a list of sanitizers enabled in this build.
constexpr base::StringPiece GetSanitizerList() {
  return ""
#if defined(ADDRESS_SANITIZER)
         "address "
#endif
#if BUILDFLAG(IS_HWASAN)
         "hwaddress "
#endif
#if defined(LEAK_SANITIZER)
         "leak "
#endif
#if defined(MEMORY_SANITIZER)
         "memory "
#endif
#if defined(THREAD_SANITIZER)
         "thread "
#endif
#if defined(UNDEFINED_SANITIZER)
         "undefined "
#endif
      ;
}

}  // namespace version_info

#endif  // COMPONENTS_VERSION_INFO_VERSION_INFO_H_

// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ENTERPRISE_PROFILE_MANAGEMENT_PROFILE_MANAGEMENT_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_ENTERPRISE_PROFILE_MANAGEMENT_PROFILE_MANAGEMENT_NAVIGATION_THROTTLE_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationHandle;
}

namespace profile_management {

extern const char kTestHost[];

// This throttle looks for profile management data in certain HTTP responses
// from supported hosts. If a response from a supported host is received, the
// navigation is deferred while trying to retrieve the data from the response
// body. If profile management data is found, this throttle triggers signin
// interception.
class ProfileManagementNavigationThrottle : public content::NavigationThrottle {
 public:
  class TokenInfoGetter {
   public:
    virtual ~TokenInfoGetter();
    // Gets the profile id and enrollment token from `navigation_handle` and
    // and calls `callback` with them. Both values will be empty strings if no
    // info is found.
    virtual void GetTokenInfo(
        content::NavigationHandle* navigation_handle,
        base::OnceCallback<void(const std::string&, const std::string&)>
            callback) = 0;
  };

  // Create a navigation throttle for the given navigation if third-party
  // profile management is enabled. Returns nullptr if no throttling should be
  // done.
  static std::unique_ptr<ProfileManagementNavigationThrottle>
  MaybeCreateThrottleFor(content::NavigationHandle* navigation_handle);

  ProfileManagementNavigationThrottle(
      content::NavigationHandle* navigation_handle,
      std::unique_ptr<TokenInfoGetter> token_info_getter);
  ~ProfileManagementNavigationThrottle() override;
  ProfileManagementNavigationThrottle(
      const ProfileManagementNavigationThrottle&) = delete;
  ProfileManagementNavigationThrottle& operator=(
      const ProfileManagementNavigationThrottle&) = delete;

  // content::NavigationThrottle implementation:
  // Looks for profile management data in the navigation response if the host is
  // supported.
  content::NavigationThrottle::ThrottleCheckResult WillProcessResponse()
      override;

  const char* GetNameForLogging() override;

 private:
  void OnTokenInfoReceived(const std::string& id,
                           const std::string& management_token);
  std::unique_ptr<TokenInfoGetter> token_info_getter_;
  base::WeakPtrFactory<ProfileManagementNavigationThrottle> weak_ptr_factory_{
      this};
};

}  // namespace profile_management

#endif  // CHROME_BROWSER_ENTERPRISE_PROFILE_MANAGEMENT_PROFILE_MANAGEMENT_NAVIGATION_THROTTLE_H_

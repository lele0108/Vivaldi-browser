// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/app/tests_hook.h"

#import "base/command_line.h"
#import "base/logging.h"
#import "components/signin/internal/identity_manager/fake_profile_oauth2_token_service.h"
#import "components/signin/internal/identity_manager/profile_oauth2_token_service.h"
#import "components/signin/internal/identity_manager/profile_oauth2_token_service_delegate.h"
#import "ios/chrome/browser/flags/chrome_switches.h"
#import "ios/chrome/browser/policy/test_platform_policy_provider.h"
#import "ios/chrome/browser/signin/fake_system_identity.h"
#import "ios/chrome/browser/signin/fake_system_identity_manager.h"
#import "ios/chrome/browser/ui/authentication/signin_earl_grey_app_interface.h"
#import "ios/chrome/test/app/chrome_test_util.h"
#import "ios/chrome/test/app/signin_test_util.h"
#import "ios/chrome/test/earl_grey/test_switches.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace tests_hook {

bool DisableAppGroupAccess() {
  return true;
}

bool DisableClientSideFieldTrials() {
  return true;
}

bool DisableContentSuggestions() {
  return true;
}

bool DisableDiscoverFeed() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableDiscoverFeed);
}

bool DisableFirstRun() {
  return true;
}

bool DisableGeolocation() {
  return true;
}

bool DisablePromoManagerFullScreenPromos() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnablePromoManagerFullscreenPromos);
}

std::unique_ptr<ProfileOAuth2TokenService> GetOverriddenTokenService(
    PrefService* user_prefs,
    std::unique_ptr<ProfileOAuth2TokenServiceDelegate> delegate) {
  // Do not fake account tracking and authentication services if the user has
  // requested a real identity manager.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          test_switches::kForceRealSystemIdentityManager)) {
    return nullptr;
  }
  std::unique_ptr<FakeProfileOAuth2TokenService> token_service =
      std::make_unique<FakeProfileOAuth2TokenService>(user_prefs,
                                                      std::move(delegate));
  // Posts auth token requests immediately on request instead of waiting for an
  // explicit `IssueTokenForScope` call.
  token_service->set_auto_post_fetch_response_on_message_loop(true);
  return token_service;
}

bool DisableUpgradeSigninPromo() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableUpgradeSigninPromo);
}

bool DisableUpdateService() {
  return true;
}

bool DisableMainThreadFreezeDetection() {
  return true;
}

bool DelayAppLaunchPromos() {
  return true;
}

policy::ConfigurationPolicyProvider* GetOverriddenPlatformPolicyProvider() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "com.apple.configuration.managed")) {
    DVLOG(1) << "Policy data present in NSUserDefaults, not installing test "
                "platform provider";
    return nullptr;
  }
  return GetTestPlatformPolicyProvider();
}

std::unique_ptr<SystemIdentityManager> CreateSystemIdentityManager() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(test_switches::kForceRealSystemIdentityManager)) {
    // By returning nullptr, we force ApplicationContext to use the provider to
    // create the SystemIdentityManager.
    return nullptr;
  }

  NSArray<id<SystemIdentity>>* identities = @[];
  if (command_line->HasSwitch(test_switches::kAddFakeIdentitiesAtStartup)) {
    const std::string command_line_value = command_line->GetSwitchValueASCII(
        test_switches::kAddFakeIdentitiesAtStartup);

    identities =
        [FakeSystemIdentity identitiesFromBase64String:command_line_value];
  }

  auto system_identity_manager =
      std::make_unique<FakeSystemIdentityManager>(identities);

  // Add a fake identity if asked to start the app in signed-in state but
  // no identity was passed via the kAddFakeIdentitiesAtStartup parameter.
  if (identities.count == 0 &&
      command_line->HasSwitch(test_switches::kSignInAtStartup)) {
    system_identity_manager->AddIdentity([FakeSystemIdentity fakeIdentity1]);
  }

  return system_identity_manager;
}

void SetUpTestsIfPresent() {
  // No-op for Earl Grey.
}

void RunTestsIfPresent() {
  // No-op for Earl Grey.
}

}  // namespace tests_hook

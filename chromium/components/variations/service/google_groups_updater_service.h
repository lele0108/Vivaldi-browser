// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VARIATIONS_SERVICE_GOOGLE_GROUPS_UPDATER_SERVICE_H_
#define COMPONENTS_VARIATIONS_SERVICE_GOOGLE_GROUPS_UPDATER_SERVICE_H_

#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace variations {
#if BUILDFLAG(IS_CHROMEOS_ASH)
extern const char kOsDogfoodGroupsSyncPrefName[];
#else
extern const char kDogfoodGroupsSyncPrefName[];
#endif
extern const char kDogfoodGroupsSyncPrefGaiaIdKey[];
}  // namespace variations

BASE_DECLARE_FEATURE(kVariationsGoogleGroupFiltering);

// Service responsible for one-way synchronization of Google group information
// from per-profile sync data to local-state.
class GoogleGroupsUpdaterService : public KeyedService {
 public:
  explicit GoogleGroupsUpdaterService(PrefService& target_prefs,
                                      const std::string& key,
                                      PrefService& source_prefs);

  GoogleGroupsUpdaterService(const GoogleGroupsUpdaterService&) = delete;
  GoogleGroupsUpdaterService& operator=(const GoogleGroupsUpdaterService&) =
      delete;

  ~GoogleGroupsUpdaterService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Clears state that should only exist for a signed in syncing user.
  // This should be called when the user signs out or disables sync, as this
  // state is server-mastered state that is a property of the account rather
  // than local state that is a property of the client.
  void ClearSigninScopedState();

 private:
  // Update the group memberships in `target_prefs_`.
  // Called when `source_prefs_` have been initialized or modified.
  void UpdateGoogleGroups();

  // The preferences to write to. These are the local-state prefs.
  // Preferences are guaranteed to outlive keyed services, so this reference
  // will stay valid for the lifetime of this service.
  const raw_ref<PrefService> target_prefs_;

  // The key to use in the `target_prefs_` dictionary.
  // This key is immutable (it will not change for eg. a given profile, even
  // across Chrome restarts).
  const std::string key_;

  // The preferences to read from. These are the profile prefs.
  // Preferences are guaranteed to outlive keyed services, so this reference
  // will stay valid for the lifetime of this service.
  const raw_ref<PrefService> source_prefs_;

  PrefChangeRegistrar pref_change_registrar_;
};

#endif  // COMPONENTS_VARIATIONS_SERVICE_GOOGLE_GROUPS_UPDATER_SERVICE_H_

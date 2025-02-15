// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/browser/apps/app_service/promise_apps/promise_app_registry_cache.h"

#include "chrome/browser/apps/app_service/package_id.h"
#include "chrome/browser/apps/app_service/promise_apps/promise_app.h"
#include "chrome/browser/apps/app_service/promise_apps/promise_app_update.h"
#include "chrome/browser/apps/app_service/promise_apps/promise_app_wrapper.h"

namespace apps {

PromiseAppRegistryCache::PromiseAppRegistryCache() = default;

PromiseAppRegistryCache::~PromiseAppRegistryCache() {
  for (auto& obs : observers_) {
    obs.OnPromiseAppRegistryCacheWillBeDestroyed(this);
  }
  CHECK(observers_.empty());
}

void PromiseAppRegistryCache::AddObserver(Observer* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void PromiseAppRegistryCache::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PromiseAppRegistryCache::OnPromiseApp(PromiseAppPtr delta) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Check that there isn't an update currently being processed. We do not allow
  // an update to trigger an observer to send and execute another update before
  // the current call completes.
  DCHECK(!update_in_progress_);
  update_in_progress_ = true;

  // Retrieve the current promise app state.
  apps::PromiseApp* state = FindPromiseApp(delta->package_id);

  for (auto& observer : observers_) {
    observer.OnPromiseAppUpdate(PromiseAppUpdate(state, delta.get()));
  }

  if (state) {
    // Update the existing promise app if it exists.
    PromiseAppUpdate::Merge(state, delta.get());
  } else {
    // Add the promise app instance to the cache if it isn't registered yet.
    promise_app_map_[delta->package_id] = delta->Clone();
  }

  update_in_progress_ = false;
}

// Retrieve a copy of all the registered promise apps.
std::vector<PromiseAppPtr> PromiseAppRegistryCache::GetAllPromiseApps() const {
  std::vector<PromiseAppPtr> promise_apps;
  for (const auto& promise_pair : promise_app_map_) {
    promise_apps.push_back(promise_pair.second.get()->Clone());
  }
  return promise_apps;
}

bool PromiseAppRegistryCache::HasPromiseApp(const PackageId& package_id) {
  return FindPromiseApp(package_id) != nullptr;
}

const PromiseApp* PromiseAppRegistryCache::GetPromiseAppForTesting(
    const PackageId& package_id) const {
  return FindPromiseApp(package_id);
}

PromiseApp* PromiseAppRegistryCache::FindPromiseApp(
    const PackageId& package_id) const {
  auto promise_iter = promise_app_map_.find(package_id);
  return (promise_iter != promise_app_map_.end()) ? promise_iter->second.get()
                                                  : nullptr;
}

}  // namespace apps

// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_APP_LIST_APP_SERVICE_APP_SERVICE_PROMISE_APP_ITEM_H_
#define CHROME_BROWSER_ASH_APP_LIST_APP_SERVICE_APP_SERVICE_PROMISE_APP_ITEM_H_

#include "chrome/browser/apps/app_service/promise_apps/promise_app_update.h"
#include "chrome/browser/ash/app_list/app_context_menu_delegate.h"
#include "chrome/browser/ash/app_list/chrome_app_list_item.h"
#include "components/services/app_service/public/cpp/app_launch_util.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/app_update.h"
#include "components/services/app_service/public/cpp/icon_types.h"

// A promise app list item provided by the App Service.
class AppServicePromiseAppItem : public ChromeAppListItem {
 public:
  static const char kItemType[];

  AppServicePromiseAppItem(Profile* profile,
                           AppListModelUpdater* model_updater,
                           const apps::PromiseAppUpdate& app_update);
  AppServicePromiseAppItem(const AppServicePromiseAppItem&) = delete;
  AppServicePromiseAppItem& operator=(const AppServicePromiseAppItem&) = delete;
  ~AppServicePromiseAppItem() override;

  // Update the promise app item with the new promise app info from the Promise
  // App Registry Cache.
  void OnPromiseAppUpdate(const apps::PromiseAppUpdate& update);

 private:
  void InitializeItem(const apps::PromiseAppUpdate& update);

  // ChromeAppListItem overrides:
  void LoadIcon() override;
  void Activate(int event_flags) override;
  const char* GetItemType() const override;
  void GetContextMenuModel(ash::AppListItemContext item_context,
                           GetMenuModelCallback callback) override;
  app_list::AppContextMenu* GetAppContextMenu() override;

  // Used to indicate the installation progress in the promise icon progress
  // bar.
  absl::optional<float> progress_;
};

#endif  // CHROME_BROWSER_ASH_APP_LIST_APP_SERVICE_APP_SERVICE_PROMISE_APP_ITEM_H_

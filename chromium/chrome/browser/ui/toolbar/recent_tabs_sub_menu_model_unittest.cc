// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/toolbar/recent_tabs_sub_menu_model.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/sessions/chrome_tab_restore_service_client.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sync/session_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/recent_tabs_builder_test_helper.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/menu_model_test.h"
#include "components/sessions/content/content_test_helper.h"
#include "components/sessions/core/serialized_navigation_entry_test_helper.h"
#include "components/sessions/core/session_types.h"
#include "components/sessions/core/tab_restore_service_impl.h"
#include "components/sync/engine/data_type_activation_response.h"
#include "components/sync/model/data_type_activation_request.h"
#include "components/sync/model/model_type_controller_delegate.h"
#include "components/sync/service/data_type_controller.h"
#include "components/sync/test/mock_commit_queue.h"
#include "components/sync_sessions/session_sync_service_impl.h"
#include "components/sync_sessions/synced_session.h"
#include "components/tab_groups/tab_group_id.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::ElementsAre;

namespace {

class TestRecentTabsMenuModelDelegate : public ui::MenuModelDelegate {
 public:
  explicit TestRecentTabsMenuModelDelegate(ui::MenuModel* model)
      : model_(model),
        got_changes_(false) {
    model_->SetMenuModelDelegate(this);
  }

  TestRecentTabsMenuModelDelegate(const TestRecentTabsMenuModelDelegate&) =
      delete;
  TestRecentTabsMenuModelDelegate& operator=(
      const TestRecentTabsMenuModelDelegate&) = delete;

  ~TestRecentTabsMenuModelDelegate() override {
    model_->SetMenuModelDelegate(nullptr);
  }

  // ui::MenuModelDelegate implementation:

  void OnIconChanged(int index) override {}

  void OnMenuStructureChanged() override { got_changes_ = true; }

  bool got_changes() const { return got_changes_; }

 private:
  raw_ptr<ui::MenuModel> model_;
  bool got_changes_;
};

struct ModelData {
  ui::MenuModel::ItemType type;
  bool enabled;
};

void VerifyModel(const ui::MenuModel& model, base::span<const ModelData> data) {
  ASSERT_EQ(data.size(), model.GetItemCount());
  for (size_t i = 0; i < data.size(); ++i) {
    SCOPED_TRACE(i);
    const ui::MenuModel::ItemType type = model.GetTypeAt(i);
    EXPECT_EQ(data[i].type, type);
    EXPECT_EQ(data[i].enabled, model.IsEnabledAt(i));
    EXPECT_EQ(type == ui::MenuModel::TYPE_TITLE, !!model.GetLabelFontListAt(i));
  }
}

void VerifyModel(const ui::MenuModel* model, base::span<const ModelData> data) {
  ASSERT_TRUE(model);
  VerifyModel(*model, std::move(data));
}

}  // namespace

class RecentTabsSubMenuModelTest
    : public BrowserWithTestWindowTest {
 public:
  RecentTabsSubMenuModelTest() = default;
  RecentTabsSubMenuModelTest(const RecentTabsSubMenuModelTest&) = delete;
  RecentTabsSubMenuModelTest& operator=(const RecentTabsSubMenuModelTest&) =
      delete;
  ~RecentTabsSubMenuModelTest() override = default;

  void WaitForLoadFromLastSession() { content::RunAllTasksUntilIdle(); }

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    session_sync_service_ = SessionSyncServiceFactory::GetForProfile(profile());

    syncer::DataTypeActivationRequest activation_request;
    activation_request.cache_guid = "test_cache_guid";
    activation_request.error_handler = base::DoNothing();

    std::unique_ptr<syncer::DataTypeActivationResponse> activation_response;
    base::RunLoop loop;
    session_sync_service_->GetControllerDelegate()->OnSyncStarting(
        activation_request,
        base::BindLambdaForTesting(
            [&](std::unique_ptr<syncer::DataTypeActivationResponse> response) {
              activation_response = std::move(response);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_NE(nullptr, activation_response);
    ASSERT_NE(nullptr, activation_response->type_processor);
    sync_processor_ = std::move(activation_response->type_processor);

    // ClientTagBasedModelTypeProcessor requires connecting before
    // other interactions with the worker happen.
    sync_processor_->ConnectSync(
        std::make_unique<testing::NiceMock<syncer::MockCommitQueue>>());
  }

  void EnableSync() {
    session_sync_service_->ProxyTabsStateChanged(
        syncer::DataTypeController::RUNNING);
  }

  void DisableSync() {
    session_sync_service_->ProxyTabsStateChanged(
        syncer::DataTypeController::NOT_RUNNING);
  }

  static std::unique_ptr<KeyedService> GetTabRestoreService(
      content::BrowserContext* browser_context) {
    return std::make_unique<sessions::TabRestoreServiceImpl>(
        base::WrapUnique(new ChromeTabRestoreServiceClient(
            Profile::FromBrowserContext(browser_context))),
        nullptr, nullptr);
  }

  void RegisterRecentTabs(RecentTabsBuilderTestHelper* helper) {
    helper->ExportToSessionSync(sync_processor_.get());
    helper->VerifyExport(static_cast<sync_sessions::SessionSyncServiceImpl*>(
                             session_sync_service_)
                             ->GetUnderlyingOpenTabsUIDelegateForTest());
  }

 private:
  raw_ptr<sync_sessions::SessionSyncService, DanglingUntriaged>
      session_sync_service_;
  std::unique_ptr<syncer::ModelTypeProcessor> sync_processor_;
};

// Test disabled "Recently closed" header with no foreign tabs.
TEST_F(RecentTabsSubMenuModelTest, NoTabs) {
  DisableSync();

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kData);
}

// Test enabled "Recently closed" header with no foreign tabs.
TEST_F(RecentTabsSubMenuModelTest, RecentlyClosedTabsFromCurrentSession) {
  DisableSync();

  TabRestoreServiceFactory::GetInstance()->SetTestingFactory(
      profile(),
      base::BindRepeating(&RecentTabsSubMenuModelTest::GetTabRestoreService));

  // Add 2 tabs and close them.
  AddTab(browser(), GURL("http://foo/1"));
  AddTab(browser(), GURL("http://foo/2"));
  browser()->tab_strip_model()->CloseAllTabs();

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // Recently closed
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for http://foo/2>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for http://foo/1>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kData);
}

// Test recently closed groups with no foreign tabs.
TEST_F(RecentTabsSubMenuModelTest, RecentlyClosedGroupsFromCurrentSession) {
  ASSERT_TRUE(browser()->tab_strip_model()->SupportsTabGroups());

  DisableSync();

  TabRestoreServiceFactory::GetInstance()->SetTestingFactory(
      profile(),
      base::BindRepeating(&RecentTabsSubMenuModelTest::GetTabRestoreService));

  AddTab(browser(), GURL("http://foo/1"));
  AddTab(browser(), GURL("http://foo/2"));
  AddTab(browser(), GURL("http://foo/3"));
  tab_groups::TabGroupId group1 =
      browser()->tab_strip_model()->AddToNewGroup({0});
  tab_groups::TabGroupId group2 =
      browser()->tab_strip_model()->AddToNewGroup({1, 2});
  browser()->tab_strip_model()->CloseAllTabsInGroup(group1);
  browser()->tab_strip_model()->CloseAllTabsInGroup(group2);

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected main menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // Recently closed
      {ui::MenuModel::TYPE_SUBMENU, true},    // <group 1>
      {ui::MenuModel::TYPE_SUBMENU, true},    // <group 0>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kData);

  // Expected group 1 menu items:
  constexpr ModelData kGroup1Data[] = {
      {ui::MenuModel::TYPE_COMMAND, true},  // Restore group
      {ui::MenuModel::TYPE_COMMAND, true},  // <tab for http://foo/2>
      {ui::MenuModel::TYPE_COMMAND, true},  // <tab for http://foo/3>
  };
  VerifyModel(model.GetSubmenuModelAt(3), kGroup1Data);

  // Expected group 0 menu items:
  constexpr ModelData kGroup0Data[] = {
      {ui::MenuModel::TYPE_COMMAND, true},  // Restore group
      {ui::MenuModel::TYPE_COMMAND, true},  // <tab for http://foo/1>
  };
  VerifyModel(model.GetSubmenuModelAt(4), kGroup0Data);
}

TEST_F(RecentTabsSubMenuModelTest,
       RecentlyClosedTabsAndWindowsFromLastSession) {
  DisableSync();

  TabRestoreServiceFactory::GetInstance()->SetTestingFactory(
      profile(),
      base::BindRepeating(&RecentTabsSubMenuModelTest::GetTabRestoreService));

  // Add 2 tabs and close them.
  AddTab(browser(), GURL("http://wnd/tab0"));
  AddTab(browser(), GURL("http://wnd/tab1"));
  browser()->tab_strip_model()->CloseAllTabs();

  // Create a SessionService for the profile (profile owns the service) and add
  // a window with two tabs to this session.
  SessionService* session_service = new SessionService(profile());
  SessionServiceFactory::SetForTestProfile(profile(),
                                           base::WrapUnique(session_service));
  const SessionID tab_id_0 = SessionID::FromSerializedValue(1);
  const SessionID tab_id_1 = SessionID::FromSerializedValue(2);
  const SessionID window_id = SessionID::FromSerializedValue(3);
  const tab_groups::TabGroupId tab_group_id =
      tab_groups::TabGroupId::GenerateNew();
  session_service->SetWindowType(window_id, Browser::TYPE_NORMAL);
  session_service->SetTabWindow(window_id, tab_id_0);
  session_service->SetTabWindow(window_id, tab_id_1);
  session_service->SetTabIndexInWindow(window_id, tab_id_0, 0);
  session_service->SetTabIndexInWindow(window_id, tab_id_1, 1);
  session_service->SetSelectedTabInWindow(window_id, 0);
  session_service->SetTabGroup(window_id, tab_id_1,
                               absl::make_optional(tab_group_id));
  session_service->UpdateTabNavigation(
      window_id, tab_id_0,
      sessions::ContentTestHelper::CreateNavigation("http://wnd1/tab0",
                                                    "title"));
  session_service->UpdateTabNavigation(
      window_id, tab_id_1,
      sessions::ContentTestHelper::CreateNavigation("http://wnd1/tab1",
                                                    "title"));
  // Set this, otherwise previous session won't be loaded.
  ExitTypeService::GetInstanceForProfile(profile())
      ->SetLastSessionExitTypeForTest(ExitType::kCrashed);
  // Move this session to the last so that TabRestoreService will load it as the
  // last session.
  SessionServiceFactory::GetForProfile(profile())->
      MoveCurrentSessionToLastSession();

  // Create a new TabRestoreService so that it'll load the recently closed tabs
  // and windows afresh.
  TabRestoreServiceFactory::GetInstance()->SetTestingFactory(
      profile(),
      base::BindRepeating(&RecentTabsSubMenuModelTest::GetTabRestoreService));
  // Let the shutdown of previous TabRestoreService run.
  content::RunAllTasksUntilIdle();

  RecentTabsSubMenuModel model(nullptr, browser());
  TestRecentTabsMenuModelDelegate delegate(&model);
  EXPECT_FALSE(delegate.got_changes());

  // Expected menu items before tabs/windows from last session are loaded:
  constexpr ModelData kDataBeforeLoad[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kDataBeforeLoad);

  // Wait for tabs from last session to be loaded.
  WaitForLoadFromLastSession();
  EXPECT_TRUE(delegate.got_changes());

  // Expected menu items after tabs/windows from last session are loaded:
  constexpr ModelData kDataAfterLoad[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // Recently closed
      {ui::MenuModel::TYPE_SUBMENU, true},    // <window>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for http://wnd0/tab1>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for http://wnd0/tab0>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kDataAfterLoad);

  constexpr ModelData kWindowSubmenuData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},  // Restore window
      {ui::MenuModel::TYPE_COMMAND, true},  // <tab for http://wnd1/tab0>
      {ui::MenuModel::TYPE_SUBMENU, true},  // <group>
  };
  const ui::MenuModel* const window_submenu = model.GetSubmenuModelAt(3);
  ASSERT_NO_FATAL_FAILURE(VerifyModel(window_submenu, kWindowSubmenuData));

  constexpr ModelData kGroupSubmenuData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},  // <tab for http://wnd1/tab1>
  };
  VerifyModel(window_submenu->GetSubmenuModelAt(2), kGroupSubmenuData);
}

// Test disabled "Recently closed" header with multiple sessions, multiple
// windows, and multiple enabled tabs from other devices.
TEST_F(RecentTabsSubMenuModelTest, OtherDevices) {
  EnableSync();

  // Tabs are populated in decreasing timestamp.
  base::Time timestamp = base::Time::Now();
  const base::TimeDelta time_delta = base::Minutes(10);

  RecentTabsBuilderTestHelper recent_tabs_builder;

  // Create session 0: 1 window, 3 tabs
  recent_tabs_builder.AddSession();
  recent_tabs_builder.AddWindow(0);
  for (int i = 0; i < 3; ++i) {
    timestamp -= time_delta;
    recent_tabs_builder.AddTabWithInfo(0, 0, timestamp, std::u16string());
  }

  // Create session 1: 2 windows, 1 tab in 1st window, 2 tabs in 2nd window
  recent_tabs_builder.AddSession();
  recent_tabs_builder.AddWindow(1);
  recent_tabs_builder.AddWindow(1);
  timestamp -= time_delta;
  recent_tabs_builder.AddTabWithInfo(1, 0, timestamp, std::u16string());
  timestamp -= time_delta;
  recent_tabs_builder.AddTabWithInfo(1, 1, timestamp, std::u16string());
  timestamp -= time_delta;
  recent_tabs_builder.AddTabWithInfo(1, 1, timestamp, std::u16string());

  RegisterRecentTabs(&recent_tabs_builder);

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session 0>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 0>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 0>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 0>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session 1>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 1 window 0>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 1 window 1>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 1 window 1>
  };
  VerifyModel(model, kData);
}

TEST_F(RecentTabsSubMenuModelTest, OtherDevicesDynamicUpdate) {
  DisableSync();

  // Before creating menu fill foreign sessions.
  base::Time update_timestamp = base::Time::Now() - base::Minutes(10);

  RecentTabsBuilderTestHelper recent_tabs_builder;

  // Create one session with one window and one tab.
  recent_tabs_builder.AddSession();
  recent_tabs_builder.AddWindow(0);
  recent_tabs_builder.AddTabWithInfo(0, 0, update_timestamp, std::u16string());

  RegisterRecentTabs(&recent_tabs_builder);

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items with sync disabled:
  constexpr ModelData kDataSyncDisabled[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // No tabs from other devices
  };
  VerifyModel(model, kDataSyncDisabled);

  EnableSync();

  // Expected menu items with sync enabled:
  constexpr ModelData kDataSyncEnabled[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
  };
  VerifyModel(model, kDataSyncEnabled);

  // Make changes dynamically.
  update_timestamp = base::Time::Now() - base::Minutes(5);

  // Add tab to the only window.
  recent_tabs_builder.AddTabWithInfo(0, 0, update_timestamp, std::u16string());

  RegisterRecentTabs(&recent_tabs_builder);

  // Expected menu items after update:
  constexpr ModelData kDataAfterUpdate[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
  };
  VerifyModel(model, kDataAfterUpdate);
}

TEST_F(RecentTabsSubMenuModelTest, MaxSessionsAndRecency) {
  EnableSync();

  // Create 4 sessions. Each session has 1 window with 1 tab.
  RecentTabsBuilderTestHelper recent_tabs_builder;
  for (int s = 0; s < 4; ++s) {
    recent_tabs_builder.AddSession();
    recent_tabs_builder.AddWindow(s);
    recent_tabs_builder.AddTab(s, 0);
  }
  RegisterRecentTabs(&recent_tabs_builder);

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session 3>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 3>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session 2>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 2>
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session 1>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab for session 1>
      // max sessions is 3, so only the 3 most recent sessions will show.
  };
  VerifyModel(model, kData);

  EXPECT_THAT(base::span<const std::u16string>(
                  recent_tabs_builder.GetTabTitlesSortedByRecency())
                  .subspan(0, 3),
              ElementsAre(model.GetLabelAt(5), model.GetLabelAt(8),
                          model.GetLabelAt(11)));
}

TEST_F(RecentTabsSubMenuModelTest, MaxTabsPerSessionAndRecency) {
  EnableSync();

  // Create a session: 2 windows with 5 tabs each.
  RecentTabsBuilderTestHelper recent_tabs_builder;
  recent_tabs_builder.AddSession();
  for (int w = 0; w < 2; ++w) {
    recent_tabs_builder.AddWindow(0);
    for (int t = 0; t < 5; ++t)
      recent_tabs_builder.AddTab(0, w);
  }
  RegisterRecentTabs(&recent_tabs_builder);

  RecentTabsSubMenuModel model(nullptr, browser());

  // Expected menu items:
  constexpr ModelData kData[] = {
      {ui::MenuModel::TYPE_COMMAND, true},    // History
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_COMMAND, false},   // Recently closed
      {ui::MenuModel::TYPE_SEPARATOR, true},  // <separator>
      {ui::MenuModel::TYPE_TITLE, false},     // <section header for session>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
      {ui::MenuModel::TYPE_COMMAND, true},    // <tab>
      // max tabs per sessions is 4, so only the 4 most tabs will show,
      // independent of which window they came from.
  };
  VerifyModel(model, kData);

  EXPECT_THAT(base::span<const std::u16string>(
                  recent_tabs_builder.GetTabTitlesSortedByRecency())
                  .subspan(0, 4),
              ElementsAre(model.GetLabelAt(5), model.GetLabelAt(6),
                          model.GetLabelAt(7), model.GetLabelAt(8)));
}

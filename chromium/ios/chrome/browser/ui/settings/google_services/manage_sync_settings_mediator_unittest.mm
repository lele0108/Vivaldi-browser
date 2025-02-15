// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/settings/google_services/manage_sync_settings_mediator.h"

#import <UIKit/UIKit.h>

#import "base/mac/foundation_util.h"
#import "components/autofill/core/common/autofill_prefs.h"
#import "components/prefs/pref_registry_simple.h"
#import "components/prefs/testing_pref_service.h"
#import "components/signin/public/identity_manager/account_info.h"
#import "components/sync/base/user_selectable_type.h"
#import "components/sync/service/sync_service.h"
#import "components/sync/test/mock_sync_service.h"
#import "ios/chrome/browser/shared/model/application_context/application_context.h"
#import "ios/chrome/browser/shared/model/browser/test/test_browser.h"
#import "ios/chrome/browser/shared/model/browser_state/test_chrome_browser_state.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_detail_icon_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_image_item.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_info_button_item.h"
#import "ios/chrome/browser/shared/ui/table_view/table_view_model.h"
#import "ios/chrome/browser/signin/authentication_service.h"
#import "ios/chrome/browser/signin/authentication_service_factory.h"
#import "ios/chrome/browser/signin/chrome_account_manager_service_factory.h"
#import "ios/chrome/browser/signin/fake_authentication_service_delegate.h"
#import "ios/chrome/browser/signin/fake_system_identity.h"
#import "ios/chrome/browser/signin/fake_system_identity_manager.h"
#import "ios/chrome/browser/signin/identity_manager_factory.h"
#import "ios/chrome/browser/sync/mock_sync_service_utils.h"
#import "ios/chrome/browser/sync/sync_service_factory.h"
#import "ios/chrome/browser/sync/sync_setup_service_factory.h"
#import "ios/chrome/browser/sync/sync_setup_service_mock.h"
#import "ios/chrome/browser/ui/settings/cells/settings_image_detail_text_item.h"
#import "ios/chrome/browser/ui/settings/cells/sync_switch_item.h"
#import "ios/chrome/browser/ui/settings/google_services/manage_sync_settings_constants.h"
#import "ios/chrome/browser/ui/settings/google_services/manage_sync_settings_consumer.h"
#import "ios/chrome/browser/ui/settings/google_services/manage_sync_settings_table_view_controller.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ios/chrome/test/ios_chrome_scoped_testing_local_state.h"
#import "ios/web/public/test/web_task_environment.h"
#import "testing/gmock/include/gmock/gmock.h"
#import "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#import "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"
#import "third_party/ocmock/ocmock_extensions.h"
#import "ui/base/l10n/l10n_util_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

PrefService* SetPrefService() {
  TestingPrefServiceSimple* prefs = new TestingPrefServiceSimple();
  PrefRegistrySimple* registry = prefs->registry();
  registry->RegisterBooleanPref(autofill::prefs::kAutofillWalletImportEnabled,
                                true);

  return prefs;
}
}  // namespace

class ManageSyncSettingsMediatorTest : public PlatformTest {
 public:
  void SetUp() override {
    PlatformTest::SetUp();

    FakeSystemIdentity* identity =
        [FakeSystemIdentity identityWithEmail:@"foo1@gmail.com"
                                       gaiaID:@"foo1ID"
                                         name:@"Fake Foo 1"];
    FakeSystemIdentityManager* system_identity_manager =
        FakeSystemIdentityManager::FromSystemIdentityManager(
            GetApplicationContext()->GetSystemIdentityManager());
    system_identity_manager->AddIdentity(identity);

    TestChromeBrowserState::Builder builder;
    builder.AddTestingFactory(SyncServiceFactory::GetInstance(),
                              base::BindRepeating(&CreateMockSyncService));
    builder.AddTestingFactory(
        SyncSetupServiceFactory::GetInstance(),
        base::BindRepeating(&SyncSetupServiceMock::CreateKeyedService));
    builder.AddTestingFactory(
        AuthenticationServiceFactory::GetInstance(),
        AuthenticationServiceFactory::GetDefaultFactory());
    browser_state_ = builder.Build();

    consumer_ = [[ManageSyncSettingsTableViewController alloc]
        initWithStyle:UITableViewStyleGrouped];
    [consumer_ loadModel];

    pref_service_ = SetPrefService();

    sync_setup_service_mock_ = static_cast<SyncSetupServiceMock*>(
        SyncSetupServiceFactory::GetForBrowserState(browser_state_.get()));
    sync_service_mock_ = static_cast<syncer::MockSyncService*>(
        SyncServiceFactory::GetForBrowserState(browser_state_.get()));
    AuthenticationServiceFactory::CreateAndInitializeForBrowserState(
        browser_state_.get(),
        std::make_unique<FakeAuthenticationServiceDelegate>());

    AuthenticationService* authentication_service =
        AuthenticationServiceFactory::GetForBrowserState(browser_state_.get());
    authentication_service->SignIn(
        identity, signin_metrics::AccessPoint::ACCESS_POINT_UNKNOWN);

    mediator_ = [[ManageSyncSettingsMediator alloc]
          initWithSyncService:sync_service_mock_
              userPrefService:pref_service_
              identityManager:IdentityManagerFactory::GetForBrowserState(
                                  browser_state_.get())
        authenticationService:authentication_service
        accountManagerService:ChromeAccountManagerServiceFactory::
                                  GetForBrowserState(browser_state_.get())
          initialAccountState:SyncSettingsAccountState::kSyncing];
    mediator_.syncSetupService = sync_setup_service_mock_;
    mediator_.consumer = consumer_;
  }

  void FirstSetupSyncOnWithConsentEnabled() {
    mediator_.initialAccountState = SyncSettingsAccountState::kSyncing;
    ON_CALL(*sync_service_mock_->GetMockUserSettings(),
            IsInitialSyncFeatureSetupComplete())
        .WillByDefault(Return(true));
    ON_CALL(*sync_service_mock_, HasSyncConsent()).WillByDefault(Return(true));
    ON_CALL(*sync_setup_service_mock_, IsSyncEverythingEnabled())
        .WillByDefault(Return(true));
    ON_CALL(*sync_service_mock_, GetTransportState())
        .WillByDefault(Return(syncer::SyncService::TransportState::ACTIVE));
    CoreAccountInfo account_info;
    account_info.email = "foo1@gmail.com";
    ON_CALL(*sync_service_mock_, GetAccountInfo())
        .WillByDefault(Return(account_info));
  }

  void FirstSetupSyncOff() {
    mediator_.initialAccountState =
        SyncSettingsAccountState::kAdvancedInitialSyncSetup;
    ON_CALL(*sync_service_mock_, HasSyncConsent()).WillByDefault(Return(false));
    ON_CALL(*sync_setup_service_mock_, IsSyncEverythingEnabled())
        .WillByDefault(Return(true));
    ON_CALL(*sync_service_mock_, GetTransportState())
        .WillByDefault(Return(syncer::SyncService::TransportState::DISABLED));
    CoreAccountInfo account_info;
    account_info.email = "foo1@gmail.com";
    ON_CALL(*sync_service_mock_, GetAccountInfo())
        .WillByDefault(Return(account_info));
  }

 protected:
  // Needed for test browser state created by TestChromeBrowserState().
  web::WebTaskEnvironment task_environment_;

  // Needed for the initialization of authentication service.
  IOSChromeScopedTestingLocalState local_state_;

  syncer::MockSyncService* sync_service_mock_;
  SyncSetupServiceMock* sync_setup_service_mock_;
  std::unique_ptr<TestChromeBrowserState> browser_state_;

  ManageSyncSettingsMediator* mediator_ = nullptr;
  ManageSyncSettingsTableViewController* consumer_ = nullptr;
  PrefService* pref_service_ = nullptr;
};

// Tests for Advanced Settings items.

// Tests that encryption is  accessible even when Sync settings have not been
// confirmed.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceSetupNotCommitted) {
  FirstSetupSyncOff();

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  EXPECT_FALSE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncSettingsSectionIdentifier::
                                         SignOutSectionIdentifier]);

  // Encryption item is enabled.
  NSArray* advanced_settings_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       AdvancedSettingsSectionIdentifier];
  ASSERT_EQ(2UL, advanced_settings_items.count);

  TableViewImageItem* encryption_item = advanced_settings_items[0];
  EXPECT_EQ(encryption_item.type, SyncSettingsItemType::EncryptionItemType);
  EXPECT_TRUE(encryption_item.enabled);
}

// Tests that encryption is accessible when there is a Sync error due to a
// missing passphrase, but Sync has otherwise been enabled.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceDisabledNeedsPassphrase) {
  FirstSetupSyncOnWithConsentEnabled();
  ON_CALL(*sync_service_mock_, GetUserActionableError())
      .WillByDefault(
          Return(syncer::SyncService::UserActionableError::kNeedsPassphrase));

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  NSArray* advanced_settings_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       AdvancedSettingsSectionIdentifier];
  ASSERT_EQ(3UL, advanced_settings_items.count);

  TableViewImageItem* encryption_item = advanced_settings_items[0];
  EXPECT_EQ(encryption_item.type, SyncSettingsItemType::EncryptionItemType);
  EXPECT_TRUE(encryption_item.enabled);
}

// Tests that encryption is accessible when Sync is enabled.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceEnabledWithEncryption) {
  FirstSetupSyncOnWithConsentEnabled();

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  NSArray* advanced_settings_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       AdvancedSettingsSectionIdentifier];
  ASSERT_EQ(3UL, advanced_settings_items.count);

  TableViewImageItem* encryption_item = advanced_settings_items[0];
  EXPECT_EQ(encryption_item.type, SyncSettingsItemType::EncryptionItemType);
  EXPECT_TRUE(encryption_item.enabled);

  EXPECT_FALSE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncErrorsSectionIdentifier]);
}

// Tests that "Turn off Sync" is hidden when Sync is disabled.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceDisabledWithTurnOffSync) {
  FirstSetupSyncOff();

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  // Sign out section not added.
  EXPECT_FALSE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncSettingsSectionIdentifier::
                                         SignOutSectionIdentifier]);
}

// Tests that "Turn off Sync" is accessible when Sync is enabled.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceEnabledWithTurnOffSync) {
  FirstSetupSyncOnWithConsentEnabled();

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  // "Turn off Sync" item is shown.
  NSArray* sign_out_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       SignOutSectionIdentifier];
  EXPECT_EQ(1UL, sign_out_items.count);
}

// Tests that the policy info is shown below the "Turn off Sync" item when the
// forced sign-in policy is enabled.
TEST_F(ManageSyncSettingsMediatorTest,
       SyncServiceEnabledWithTurnOffSyncWithForcedSigninPolicy) {
  FirstSetupSyncOnWithConsentEnabled();

  mediator_.forcedSigninEnabled = YES;

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  // "Turn off Sync" item is shown.
  NSArray* sign_out_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       SignOutSectionIdentifier];
  EXPECT_EQ(1UL, sign_out_items.count);

  // The footer below "Turn off Sync" is shown.
  ListItem* footer = [mediator_.consumer.tableViewModel
      footerForSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                         SignOutSectionIdentifier];
  TableViewLinkHeaderFooterItem* footerTextItem =
      base::mac::ObjCCastStrict<TableViewLinkHeaderFooterItem>(footer);
  EXPECT_GT([footerTextItem.text length], 0UL);
}

// Tests that a Sync error that occurs after the user has loaded the Settings
// page once will update the full page.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceSuccessThenDisabled) {
  FirstSetupSyncOnWithConsentEnabled();
  EXPECT_CALL(*sync_service_mock_, GetDisableReasons())
      .WillOnce(Return(syncer::SyncService::DisableReasonSet()))
      .WillOnce(Return(syncer::SyncService::DisableReasonSet(
          {syncer::SyncService::DISABLE_REASON_ENTERPRISE_POLICY})));

  // Loads the Sync page once in success state.
  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];
  // Loads the Sync page again in disabled state.
  [mediator_ onSyncStateChanged];

  EXPECT_TRUE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncSettingsSectionIdentifier::
                                         SyncErrorsSectionIdentifier]);
  NSArray* error_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       SyncErrorsSectionIdentifier];
  EXPECT_EQ(1UL, error_items.count);
}

// Tests that Sync errors display a single error message when loaded one after
// the other.
TEST_F(ManageSyncSettingsMediatorTest, SyncServiceMultipleErrors) {
  FirstSetupSyncOnWithConsentEnabled();
  ON_CALL(*sync_service_mock_, GetUserActionableError())
      .WillByDefault(
          Return(syncer::SyncService::UserActionableError::kNeedsPassphrase));
  EXPECT_CALL(*sync_service_mock_, GetDisableReasons())
      .WillOnce(Return(syncer::SyncService::DisableReasonSet()))
      .WillOnce(Return(syncer::SyncService::DisableReasonSet(
          {syncer::SyncService::DISABLE_REASON_ENTERPRISE_POLICY})));

  // Loads the Sync page once in the disabled by enterprise policy error state.
  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];
  // Loads the Sync page again in the needs encryption passphrase error state.
  [mediator_ onSyncStateChanged];

  EXPECT_TRUE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncSettingsSectionIdentifier::
                                         SyncErrorsSectionIdentifier]);
  NSArray* error_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       SyncErrorsSectionIdentifier];
  ASSERT_EQ(1UL, error_items.count);
  TableViewDetailIconItem* error_item =
      base::mac::ObjCCastStrict<TableViewDetailIconItem>(error_items[0]);
  EXPECT_NSEQ(
      error_item.detailText,
      l10n_util::GetNSString(
          IDS_IOS_GOOGLE_SERVICES_SETTINGS_ENTER_PASSPHRASE_TO_START_SYNC));
}

// Tests that "Turn off Sync" item transition from disabled to enabled goes from
// hiding to showing the item.
TEST_F(ManageSyncSettingsMediatorTest,
       SyncServiceSetupTransitionForTurnOffSync) {
  // Set Sync disabled expectations.
  FirstSetupSyncOff();

  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  // Sign out section not added.
  EXPECT_FALSE([mediator_.consumer.tableViewModel
      hasSectionForSectionIdentifier:SyncSettingsSectionIdentifier::
                                         SignOutSectionIdentifier]);

  // Set Sync enabled expectations.
  FirstSetupSyncOnWithConsentEnabled();

  // Loads the Sync page again in enabled state.
  [mediator_ onSyncStateChanged];

  // "Turn off Sync" item is shown.
  NSArray* shown_sign_out_items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncSettingsSectionIdentifier::
                                       SignOutSectionIdentifier];
  EXPECT_EQ(1UL, shown_sign_out_items.count);
}

// Tests that the items are correct when a sync type list is managed.
TEST_F(ManageSyncSettingsMediatorTest,
       CheckItemsWhenSyncTypeListHasEnabledItems) {
  FirstSetupSyncOnWithConsentEnabled();

  // Set up a policy to disable bookmarks and passwords.
  ON_CALL(*sync_service_mock_->GetMockUserSettings(),
          IsTypeManagedByPolicy(syncer::UserSelectableType::kBookmarks))
      .WillByDefault(Return(true));
  ON_CALL(*sync_service_mock_->GetMockUserSettings(),
          IsTypeManagedByPolicy(syncer::UserSelectableType::kPasswords))
      .WillByDefault(Return(true));

  // Loads the Sync page.
  [mediator_ manageSyncSettingsTableViewControllerLoadModel:mediator_.consumer];

  // Check sync switches.
  NSArray* items = [mediator_.consumer.tableViewModel
      itemsInSectionWithIdentifier:SyncDataTypeSectionIdentifier];
  for (TableViewItem* item in items) {
    if (item.type == SyncEverythingItemType) {
      EXPECT_FALSE([item isKindOfClass:[SyncSwitchItem class]]);
      continue;
    } else if (item.type == BookmarksDataTypeItemType ||
               item.type == PasswordsDataTypeItemType) {
      EXPECT_TRUE([item isKindOfClass:[TableViewInfoButtonItem class]]);
      continue;
    }
    SyncSwitchItem* switch_item =
        base::mac::ObjCCastStrict<SyncSwitchItem>(item);
    if (switch_item.type == AutocompleteWalletItemType) {
      EXPECT_FALSE(switch_item.enabled);
    } else {
      EXPECT_TRUE(switch_item.enabled);
    }
  }
}

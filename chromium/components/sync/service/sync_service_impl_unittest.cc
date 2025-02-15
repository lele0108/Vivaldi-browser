// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/service/sync_service_impl.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "components/sync/base/command_line_switches.h"
#include "components/sync/base/features.h"
#include "components/sync/base/model_type.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/base/stop_source.h"
#include "components/sync/base/sync_util.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/engine/nigori/key_derivation_params.h"
#include "components/sync/service/configure_context.h"
#include "components/sync/service/data_type_manager_impl.h"
#include "components/sync/service/sync_service_observer.h"
#include "components/sync/service/sync_token_status.h"
#include "components/sync/test/fake_data_type_controller.h"
#include "components/sync/test/fake_sync_api_component_factory.h"
#include "components/sync/test/fake_sync_engine.h"
#include "components/sync/test/mock_trusted_vault_client.h"
#include "components/sync/test/sync_client_mock.h"
#include "components/sync/test/sync_service_impl_bundle.h"
#include "components/version_info/version_info_values.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::AtLeast;
using testing::ByMove;
using testing::Eq;
using testing::Invoke;
using testing::IsNull;
using testing::Not;
using testing::Return;

namespace syncer {

namespace {

MATCHER_P(ContainsDataType, type, "") {
  return arg.Has(type);
}

constexpr char kTestUser[] = "test_user@gmail.com";

class MockSyncServiceObserver : public SyncServiceObserver {
 public:
  MOCK_METHOD(void, OnStateChanged, (SyncService * sync), (override));
};

class TestSyncServiceObserver : public SyncServiceObserver {
 public:
  TestSyncServiceObserver() = default;

  void OnStateChanged(SyncService* sync) override {
    setup_in_progress_ = sync->IsSetupInProgress();
    auth_error_ = sync->GetAuthError();
  }

  bool setup_in_progress() const { return setup_in_progress_; }
  GoogleServiceAuthError auth_error() const { return auth_error_; }

 private:
  bool setup_in_progress_ = false;
  GoogleServiceAuthError auth_error_;
};

// A test harness that uses a real SyncServiceImpl and in most cases a
// FakeSyncEngine.
//
// This is useful if we want to test the SyncServiceImpl and don't care about
// testing the SyncEngine.
class SyncServiceImplTest : public ::testing::Test {
 protected:
  SyncServiceImplTest() = default;
  ~SyncServiceImplTest() override = default;

  void SetUp() override {
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        kSyncDeferredStartupTimeoutSeconds, "0");
  }

  void TearDown() override {
    // Kill the service before the profile.
    ShutdownAndDeleteService();
  }

  void SignIn() {
    identity_test_env()->MakePrimaryAccountAvailable(
        kTestUser, signin::ConsentLevel::kSync);
  }

  void CreateService(std::vector<std::pair<ModelType, bool>>
                         registered_types_and_transport_mode_support = {
                             {BOOKMARKS, false},
                             {DEVICE_INFO, true}}) {
    DCHECK(!service_);

    // Default includes a regular controller and a transport-mode controller.
    DataTypeController::TypeVector controllers;
    for (const auto& [type, transport_mode_support] :
         registered_types_and_transport_mode_support) {
      auto controller = std::make_unique<FakeDataTypeController>(
          type, transport_mode_support);
      // Hold a raw pointer to directly interact with the controller.
      controller_map_[type] = controller.get();
      controllers.push_back(std::move(controller));
    }

    std::unique_ptr<SyncClientMock> sync_client =
        sync_service_impl_bundle_.CreateSyncClientMock();
    sync_client_ = sync_client.get();
    ON_CALL(*sync_client, CreateDataTypeControllers)
        .WillByDefault(Return(ByMove(std::move(controllers))));

    service_ = std::make_unique<SyncServiceImpl>(
        sync_service_impl_bundle_.CreateBasicInitParams(
            std::move(sync_client)));
  }

  void CreateServiceWithLocalSyncBackend() {
    DCHECK(!service_);

    // Include a regular controller and a transport-mode controller.
    DataTypeController::TypeVector controllers;
    controllers.push_back(std::make_unique<FakeDataTypeController>(BOOKMARKS));
    controllers.push_back(std::make_unique<FakeDataTypeController>(
        DEVICE_INFO, /*enable_transport_only_modle=*/true));

    std::unique_ptr<SyncClientMock> sync_client =
        sync_service_impl_bundle_.CreateSyncClientMock();
    sync_client_ = sync_client.get();
    ON_CALL(*sync_client, CreateDataTypeControllers)
        .WillByDefault(Return(ByMove(std::move(controllers))));

    SyncServiceImpl::InitParams init_params =
        sync_service_impl_bundle_.CreateBasicInitParams(std::move(sync_client));

    prefs()->SetBoolean(prefs::kEnableLocalSyncBackend, true);
    init_params.identity_manager = nullptr;

    service_ = std::make_unique<SyncServiceImpl>(std::move(init_params));
  }

  void ShutdownAndDeleteService() {
    if (service_) {
      service_->Shutdown();
    }
    service_.reset();
  }

  void PopulatePrefsForNthSync() {
    component_factory()->set_first_time_sync_configure_done(true);
    // Set first sync time before initialize to simulate a complete sync setup.
    SyncPrefs sync_prefs(prefs());
    sync_prefs.SetSyncRequested(true);
    sync_prefs.SetSelectedTypes(
        /*keep_everything_synced=*/true,
        /*registered_types=*/UserSelectableTypeSet::All(),
        /*selected_types=*/UserSelectableTypeSet::All());
    sync_prefs.SetInitialSyncFeatureSetupComplete();
  }

  void InitializeForNthSync(bool run_until_idle = true) {
    PopulatePrefsForNthSync();
    service_->Initialize();
    if (run_until_idle) {
      task_environment_.RunUntilIdle();
    }
  }

  void InitializeForFirstSync(bool run_until_idle = true) {
    service_->Initialize();
    if (run_until_idle) {
      task_environment_.RunUntilIdle();
    }
  }

  void SetInvalidationsEnabled() {
    SyncStatus status = engine()->GetDetailedStatus();
    status.notifications_enabled = true;
    engine()->SetDetailedStatus(status);
    service()->OnInvalidationStatusChanged();
  }

  void TriggerPassphraseRequired() {
    service_->GetEncryptionObserverForTest()->OnPassphraseRequired(
        KeyDerivationParams::CreateForPbkdf2(), sync_pb::EncryptedData());
  }

  signin::IdentityManager* identity_manager() {
    return sync_service_impl_bundle_.identity_manager();
  }

  signin::IdentityTestEnvironment* identity_test_env() {
    return sync_service_impl_bundle_.identity_test_env();
  }

  SyncServiceImpl* service() { return service_.get(); }

  SyncClientMock* sync_client() { return sync_client_; }

  TestingPrefServiceSimple* prefs() {
    return sync_service_impl_bundle_.pref_service();
  }

  FakeSyncApiComponentFactory* component_factory() {
    return sync_service_impl_bundle_.component_factory();
  }

  DataTypeManagerImpl* data_type_manager() {
    return component_factory()->last_created_data_type_manager();
  }

  FakeSyncEngine* engine() {
    return component_factory()->last_created_engine();
  }

  MockSyncInvalidationsService* sync_invalidations_service() {
    return sync_service_impl_bundle_.sync_invalidations_service();
  }

  MockTrustedVaultClient* trusted_vault_client() {
    return sync_service_impl_bundle_.trusted_vault_client();
  }

  FakeDataTypeController* get_controller(ModelType type) {
    return controller_map_[type];
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  SyncServiceImplBundle sync_service_impl_bundle_;
  std::unique_ptr<SyncServiceImpl> service_;
  raw_ptr<SyncClientMock, DanglingUntriaged>
      sync_client_;  // Owned by |service_|.
  // The controllers are owned by |service_|.
  std::map<ModelType, FakeDataTypeController*> controller_map_;
};

// Verify that the server URLs are sane.
TEST_F(SyncServiceImplTest, InitialState) {
  CreateService();
  InitializeForNthSync();
  const std::string& url = service()->GetSyncServiceUrlForDebugging().spec();
  EXPECT_TRUE(url == internal::kSyncServerUrl ||
              url == internal::kSyncDevServerUrl);
}

TEST_F(SyncServiceImplTest, SuccessfulInitialization) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
}

TEST_F(SyncServiceImplTest, SuccessfulLocalBackendInitialization) {
  CreateServiceWithLocalSyncBackend();
  InitializeForNthSync();
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
}

// ChromeOS Ash sets FirstSetupComplete automatically.
#if !BUILDFLAG(IS_CHROMEOS_ASH)
// Verify that an initialization where first setup is not complete does not
// start up Sync-the-feature.
TEST_F(SyncServiceImplTest, NeedsConfirmation) {
  SignIn();
  CreateService();

  // Mimic a sync cycle (transport-only) having completed earlier.
  SyncPrefs sync_prefs(prefs());
  sync_prefs.SetSyncRequested(true);
  sync_prefs.SetSelectedTypes(
      /*keep_everything_synced=*/true,
      /*registered_types=*/UserSelectableTypeSet::All(),
      /*selected_types=*/UserSelectableTypeSet::All());
  service()->Initialize();

  EXPECT_TRUE(service()->GetDisableReasons().Empty());

  // Sync should immediately start up in transport mode.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_FALSE(service()->IsSyncFeatureActive());
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
}
#endif

TEST_F(SyncServiceImplTest, ModelTypesForTransportMode) {
  SignIn();
  CreateService();
  InitializeForFirstSync();

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Sync-the-feature is normally enabled in Ash. Triggering a dashboard reset
  // is one way to achieve otherwise.
  SyncProtocolError client_cmd;
  client_cmd.action = DISABLE_SYNC_ON_CLIENT;
  service()->OnActionableProtocolError(client_cmd);
#endif

  ASSERT_FALSE(service()->IsSyncFeatureActive());
  ASSERT_FALSE(service()->IsSyncFeatureEnabled());

  // Sync-the-transport should become active.
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // ModelTypes for sync-the-feature are not configured.
  EXPECT_FALSE(service()->GetActiveDataTypes().Has(BOOKMARKS));

  // ModelTypes for sync-the-transport are configured.
  EXPECT_TRUE(service()->GetActiveDataTypes().Has(DEVICE_INFO));
}

// Verify that the SetSetupInProgress function call updates state
// and notifies observers.
TEST_F(SyncServiceImplTest, SetupInProgress) {
  CreateService();
  InitializeForFirstSync();

  TestSyncServiceObserver observer;
  service()->AddObserver(&observer);

  std::unique_ptr<SyncSetupInProgressHandle> sync_blocker =
      service()->GetSetupInProgressHandle();
  EXPECT_TRUE(observer.setup_in_progress());
  sync_blocker.reset();
  EXPECT_FALSE(observer.setup_in_progress());

  service()->RemoveObserver(&observer);
}

// Verify that disable by enterprise policy works.
TEST_F(SyncServiceImplTest, DisabledByPolicyBeforeInit) {
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(true));
  SignIn();
  CreateService();
  InitializeForNthSync();
  // Sync was disabled due to the policy.
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_ENTERPRISE_POLICY}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());
}

class SyncServiceImplTestWithIgnoreSyncRequestedFeature
    : public SyncServiceImplTest,
      public ::testing::WithParamInterface<bool> {
 public:
  SyncServiceImplTestWithIgnoreSyncRequestedFeature() {
#if !BUILDFLAG(IS_CHROMEOS_ASH)
    scoped_feature_list_.InitWithFeatureState(
        kSyncIgnoreSyncRequestedPreference, GetParam());
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)
  }

  ~SyncServiceImplTestWithIgnoreSyncRequestedFeature() override = default;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_P(SyncServiceImplTestWithIgnoreSyncRequestedFeature,
       DisabledByPolicyBeforeInitThenPolicyRemoved) {
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(true));
  SignIn();

  CreateService();

  InitializeForNthSync();
  // Sync was disabled due to the policy.
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_ENTERPRISE_POLICY}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());

  // Remove the policy.
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(false));
  base::RunLoop().RunUntilIdle();

  // The transport becomes active, but sync-the-feature remains off until the
  // user takes some action, where the precise action depends on the platform.
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(service()->IsSyncFeatureActive());
  EXPECT_TRUE(service()->GetDisableReasons().Empty());

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // On ChromeOS Ash, the first setup is marked as complete automatically.
  ASSERT_TRUE(
      service()->GetUserSettings()->IsInitialSyncFeatureSetupComplete());

  // On ChromeOS Ash, sync-the-feature stays disabled even after the policy is
  // removed, for historic reasons. It is unclear if this behavior is optional,
  // because it is indistinguishable from the sync-reset-via-dashboard case.
  // It can be resolved by invoking SetSyncFeatureRequested().
  EXPECT_TRUE(service()->IsSyncFeatureDisabledViaDashboard());
  service()->SetSyncFeatureRequested();

#else
  // For any platform except ChromeOS Ash, the user needs to turn sync on
  // manually.
  ASSERT_FALSE(
      service()->GetUserSettings()->IsInitialSyncFeatureSetupComplete());
  service()->SetSyncFeatureRequested();
  service()->GetUserSettings()->SetInitialSyncFeatureSetupComplete(
      syncer::SyncFirstSetupCompleteSource::BASIC_FLOW);
  base::RunLoop().RunUntilIdle();
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Sync-the-feature is considered on.
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_TRUE(service()->IsSyncFeatureEnabled());
  EXPECT_TRUE(service()->IsSyncFeatureActive());
}

INSTANTIATE_TEST_SUITE_P(SyncIgnoreSyncRequestedPreference,
                         SyncServiceImplTestWithIgnoreSyncRequestedFeature,
                         ::testing::Values(false, true));

// Verify that disable by enterprise policy works even after the backend has
// been initialized.
TEST_F(SyncServiceImplTest, DisabledByPolicyAfterInit) {
  SignIn();
  CreateService();
  InitializeForNthSync();

  ASSERT_EQ(SyncService::DisableReasonSet(), service()->GetDisableReasons());
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(true));

  // Sync was disabled due to the policy.
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_ENTERPRISE_POLICY}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());
}

// Exercises the SyncServiceImpl's code paths related to getting shut down
// before the backend initialize call returns.
TEST_F(SyncServiceImplTest, AbortedByShutdown) {
  SignIn();
  CreateService();
  component_factory()->AllowFakeEngineInitCompletion(false);

  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::INITIALIZING,
            service()->GetTransportState());

  ShutdownAndDeleteService();
}

// Certain SyncServiceImpl tests don't apply to Chrome OS, for example
// things that deal with concepts like "signing out".
#if !BUILDFLAG(IS_CHROMEOS_ASH)
// Test the user signing out before the backend's initialization completes.
TEST_F(SyncServiceImplTest, EarlySignOut) {
  SignIn();
  CreateService();
  // Set up a fake sync engine that will not immediately finish initialization.
  component_factory()->AllowFakeEngineInitCompletion(false);
  InitializeForNthSync();

  ASSERT_EQ(SyncService::TransportState::INITIALIZING,
            service()->GetTransportState());

  // Sign-out.
  signin::PrimaryAccountMutator* account_mutator =
      identity_manager()->GetPrimaryAccountMutator();
  DCHECK(account_mutator) << "Account mutator should only be null on ChromeOS.";
  account_mutator->ClearPrimaryAccount(
      signin_metrics::ProfileSignout::kTest,
      signin_metrics::SignoutDelete::kIgnoreMetric);
  // Wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_NOT_SIGNED_IN}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());
  EXPECT_FALSE(service()->IsSyncFeatureActive());
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
}
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)

// Certain SyncServiceImpl tests don't apply to Chrome OS, for example
// things that deal with concepts like "signing out".
#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(SyncServiceImplTest, SignOutDisablesSyncTransportAndSyncFeature) {
  // Sign-in and enable sync.
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::DisableReasonSet(), service()->GetDisableReasons());
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // Sign-out.
  signin::PrimaryAccountMutator* account_mutator =
      identity_manager()->GetPrimaryAccountMutator();
  DCHECK(account_mutator) << "Account mutator should only be null on ChromeOS.";
  account_mutator->ClearPrimaryAccount(
      signin_metrics::ProfileSignout::kTest,
      signin_metrics::SignoutDelete::kIgnoreMetric);
  // Wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_NOT_SIGNED_IN}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());
}

TEST_F(SyncServiceImplTest,
       SignOutClearsSyncTransportDataAndSyncTheFeaturePrefs) {
  // Sign-in and enable sync.
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_TRUE(
      service()->GetUserSettings()->IsInitialSyncFeatureSetupComplete());
  ASSERT_EQ(SyncService::DisableReasonSet(), service()->GetDisableReasons());
  ASSERT_EQ(0, component_factory()->clear_transport_data_call_count());

  // Sign-out.
  signin::PrimaryAccountMutator* account_mutator =
      identity_manager()->GetPrimaryAccountMutator();
  DCHECK(account_mutator) << "Account mutator should only be null on ChromeOS.";
  account_mutator->ClearPrimaryAccount(
      signin_metrics::ProfileSignout::kTest,
      signin_metrics::SignoutDelete::kIgnoreMetric);
  // Wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();
  // These are specific to sync-the-feature and should be cleared.
  EXPECT_FALSE(
      service()->GetUserSettings()->IsInitialSyncFeatureSetupComplete());
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_NOT_SIGNED_IN}),
            service()->GetDisableReasons());
  EXPECT_EQ(1, component_factory()->clear_transport_data_call_count());
#if BUILDFLAG(IS_IOS)
  SyncPrefs sync_prefs(prefs());
  EXPECT_FALSE(
      sync_prefs.IsOptedInForBookmarksAndReadingListAccountStorageForTesting());
#endif  // BUILDFLAG(IS_IOS)
}

TEST_F(SyncServiceImplTest,
       SignOutDuringTransportModeClearsTransportDataAndAccountStorageOptIn) {
  // Sign-in.
  SignIn();
  CreateService();
  InitializeForFirstSync();

  ASSERT_FALSE(service()->IsSyncFeatureActive());
  ASSERT_FALSE(service()->IsSyncFeatureEnabled());

  // Sync-the-transport should become active.
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

#if BUILDFLAG(IS_IOS)
  // Opt in bookmarks and reading list account storage.
  SyncPrefs sync_prefs(prefs());
  sync_prefs.SetBookmarksAndReadingListAccountStorageOptIn(true);
#endif  // BUILDFLAG(IS_IOS)

  // Sign-out.
  signin::PrimaryAccountMutator* account_mutator =
      identity_manager()->GetPrimaryAccountMutator();
  DCHECK(account_mutator) << "Account mutator should only be null on ChromeOS.";
  account_mutator->ClearPrimaryAccount(
      signin_metrics::ProfileSignout::kTest,
      signin_metrics::SignoutDelete::kIgnoreMetric);
  // Wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, component_factory()->clear_transport_data_call_count());
#if BUILDFLAG(IS_IOS)
  EXPECT_FALSE(
      sync_prefs.IsOptedInForBookmarksAndReadingListAccountStorageForTesting());
#endif  // BUILDFLAG(IS_IOS)
}
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)

TEST_F(SyncServiceImplTest, GetSyncTokenStatus) {
  SignIn();
  CreateService();
  InitializeForNthSync(/*run_until_idle=*/false);

  // Initial status: The Sync engine startup has not begun yet; no token request
  // has been sent.
  SyncTokenStatus token_status = service()->GetSyncTokenStatusForDebugging();
  ASSERT_EQ(CONNECTION_NOT_ATTEMPTED, token_status.connection_status);
  ASSERT_TRUE(token_status.connection_status_update_time.is_null());
  ASSERT_TRUE(token_status.token_request_time.is_null());
  ASSERT_TRUE(token_status.token_response_time.is_null());
  ASSERT_FALSE(token_status.has_token);

  // Sync engine startup as well as the actual token request take the form of
  // posted tasks. Run them.
  base::RunLoop().RunUntilIdle();

  // Now we should have an access token.
  token_status = service()->GetSyncTokenStatusForDebugging();
  EXPECT_TRUE(token_status.connection_status_update_time.is_null());
  EXPECT_FALSE(token_status.token_request_time.is_null());
  EXPECT_FALSE(token_status.token_response_time.is_null());
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            token_status.last_get_token_error);
  EXPECT_TRUE(token_status.next_token_request_time.is_null());
  EXPECT_TRUE(token_status.has_token);

  // Simulate an auth error.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  // This should get reflected in the status, and we should have dropped the
  // invalid access token.
  token_status = service()->GetSyncTokenStatusForDebugging();
  EXPECT_EQ(CONNECTION_AUTH_ERROR, token_status.connection_status);
  EXPECT_FALSE(token_status.connection_status_update_time.is_null());
  EXPECT_FALSE(token_status.token_request_time.is_null());
  EXPECT_FALSE(token_status.token_response_time.is_null());
  EXPECT_EQ(GoogleServiceAuthError::AuthErrorNone(),
            token_status.last_get_token_error);
  EXPECT_FALSE(token_status.next_token_request_time.is_null());
  EXPECT_FALSE(token_status.has_token);

  // Simulate successful connection.
  service()->OnConnectionStatusChange(CONNECTION_OK);
  token_status = service()->GetSyncTokenStatusForDebugging();
  EXPECT_EQ(CONNECTION_OK, token_status.connection_status);
}

TEST_F(SyncServiceImplTest, RevokeAccessTokenFromTokenService) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  const CoreAccountId primary_account_id =
      identity_manager()->GetPrimaryAccountId(signin::ConsentLevel::kSync);

  // Make sure the expected account_id was passed to the SyncEngine.
  ASSERT_EQ(primary_account_id, engine()->authenticated_account_id());

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(service()->GetAccessTokenForTest().empty());

  AccountInfo secondary_account_info =
      identity_test_env()->MakeAccountAvailable("test_user2@gmail.com");
  identity_test_env()->RemoveRefreshTokenForAccount(
      secondary_account_info.account_id);
  EXPECT_FALSE(service()->GetAccessTokenForTest().empty());

  identity_test_env()->RemoveRefreshTokenForPrimaryAccount();
  EXPECT_TRUE(service()->GetAccessTokenForTest().empty());
}

// Checks that CREDENTIALS_REJECTED_BY_CLIENT resets the access token and stops
// Sync. Regression test for https://crbug.com/824791.
TEST_F(SyncServiceImplTest, CredentialsRejectedByClient_StopSync) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  TestSyncServiceObserver observer;
  service()->AddObserver(&observer);

  const CoreAccountId primary_account_id =
      identity_manager()->GetPrimaryAccountId(signin::ConsentLevel::kSync);

  // Make sure the expected account_id was passed to the SyncEngine.
  ASSERT_EQ(primary_account_id, engine()->authenticated_account_id());

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(service()->GetAccessTokenForTest().empty());
  ASSERT_EQ(GoogleServiceAuthError::AuthErrorNone(), service()->GetAuthError());
  ASSERT_EQ(GoogleServiceAuthError::AuthErrorNone(), observer.auth_error());

  // Simulate the credentials getting locally rejected by the client by setting
  // the refresh token to a special invalid value.
  identity_test_env()->SetInvalidRefreshTokenForPrimaryAccount();
  const GoogleServiceAuthError rejected_by_client =
      GoogleServiceAuthError::FromInvalidGaiaCredentialsReason(
          GoogleServiceAuthError::InvalidGaiaCredentialsReason::
              CREDENTIALS_REJECTED_BY_CLIENT);
  ASSERT_EQ(rejected_by_client,
            identity_test_env()
                ->identity_manager()
                ->GetErrorStateOfRefreshTokenForAccount(primary_account_id));
  EXPECT_TRUE(service()->GetAccessTokenForTest().empty());

  // The observer should have been notified of the auth error state.
  EXPECT_EQ(rejected_by_client, observer.auth_error());
  // The Sync engine should have been shut down.
  EXPECT_FALSE(service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::TransportState::PAUSED,
            service()->GetTransportState());

  service()->RemoveObserver(&observer);
}

// CrOS Ash does not support signout.
#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(SyncServiceImplTest, SignOutRevokeAccessToken) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  const CoreAccountId primary_account_id =
      identity_manager()->GetPrimaryAccountId(signin::ConsentLevel::kSync);

  // Make sure the expected account_id was passed to the SyncEngine.
  ASSERT_EQ(primary_account_id, engine()->authenticated_account_id());

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(service()->GetAccessTokenForTest().empty());

  signin::PrimaryAccountMutator* account_mutator =
      identity_manager()->GetPrimaryAccountMutator();

  // GetPrimaryAccountMutator() returns nullptr on ChromeOS only.
  DCHECK(account_mutator);

  account_mutator->ClearPrimaryAccount(
      signin_metrics::ProfileSignout::kTest,
      signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_TRUE(service()->GetAccessTokenForTest().empty());
}
#endif

TEST_F(SyncServiceImplTest, StopAndClearWillClearDataAndSwitchToTransportMode) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  ASSERT_EQ(0, component_factory()->clear_transport_data_call_count());

  service()->StopAndClear();

  // Even though Sync-the-feature is disabled, there's still an (unconsented)
  // signed-in account, so Sync-the-transport should still be running.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
  EXPECT_EQ(1, component_factory()->clear_transport_data_call_count());
}

// Verify that sync transport data is cleared when the service is initializing
// and account is signed out.
TEST_F(SyncServiceImplTest, ClearTransportDataOnInitializeWhenSignedOut) {
  // Clearing prefs can be triggered only after `IdentityManager` finishes
  // loading the list of accounts, so wait for it to complete.
  identity_test_env()->WaitForRefreshTokensLoaded();

  // Don't sign-in before creating the service.
  CreateService();

  ASSERT_EQ(0, component_factory()->clear_transport_data_call_count());

  // Initialize when signed out to trigger clearing of prefs.
  InitializeForNthSync();

  EXPECT_EQ(1, component_factory()->clear_transport_data_call_count());
}

TEST_F(SyncServiceImplTest, StopSyncAndClearTwiceDoesNotCrash) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // Disable sync.
  service()->StopAndClear();
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());

  // Calling StopAndClear while already stopped should not crash. This may
  // (under some circumstances) happen when the user enables sync again but hits
  // the cancel button at the end of the process.
  service()->StopAndClear();
  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
}

// Verify that credential errors get returned from GetAuthError().
TEST_F(SyncServiceImplTest, CredentialErrorReturned) {
  // This test needs to manually send access tokens (or errors), so disable
  // automatic replies to access token requests.
  identity_test_env()->SetAutomaticIssueOfAccessTokens(false);

  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  const CoreAccountId primary_account_id =
      identity_manager()->GetPrimaryAccountId(signin::ConsentLevel::kSync);

  // Make sure the expected account_id was passed to the SyncEngine.
  ASSERT_EQ(primary_account_id, engine()->authenticated_account_id());

  TestSyncServiceObserver observer;
  service()->AddObserver(&observer);

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  // Wait for SyncServiceImpl to send an access token request.
  base::RunLoop().RunUntilIdle();
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      primary_account_id, "access token", base::Time::Max());
  ASSERT_FALSE(service()->GetAccessTokenForTest().empty());
  ASSERT_EQ(GoogleServiceAuthError::NONE, service()->GetAuthError().state());

  // Emulate Chrome receiving a new, invalid LST. This happens when the user
  // signs out of the content area.
  identity_test_env()->SetRefreshTokenForPrimaryAccount();
  // Again, wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithError(
      GoogleServiceAuthError(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));

  // Check that the invalid token is returned from sync.
  EXPECT_EQ(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS,
            service()->GetAuthError().state());
  EXPECT_EQ(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS,
            observer.auth_error().state());
  // Sync should pause.
  EXPECT_EQ(SyncService::TransportState::PAUSED,
            service()->GetTransportState());

  service()->RemoveObserver(&observer);
}

// Verify that credential errors get cleared when a new token is fetched
// successfully.
TEST_F(SyncServiceImplTest, CredentialErrorClearsOnNewToken) {
  // This test needs to manually send access tokens (or errors), so disable
  // automatic replies to access token requests.
  identity_test_env()->SetAutomaticIssueOfAccessTokens(false);

  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  const CoreAccountId primary_account_id =
      identity_manager()->GetPrimaryAccountId(signin::ConsentLevel::kSync);

  // Make sure the expected account_id was passed to the SyncEngine.
  ASSERT_EQ(primary_account_id, engine()->authenticated_account_id());

  TestSyncServiceObserver observer;
  service()->AddObserver(&observer);

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  // Wait for SyncServiceImpl to send an access token request.
  base::RunLoop().RunUntilIdle();
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      primary_account_id, "access token", base::Time::Max());
  ASSERT_FALSE(service()->GetAccessTokenForTest().empty());
  ASSERT_EQ(GoogleServiceAuthError::NONE, service()->GetAuthError().state());

  // Emulate Chrome receiving a new, invalid LST. This happens when the user
  // signs out of the content area.
  identity_test_env()->SetRefreshTokenForPrimaryAccount();
  // Wait for SyncServiceImpl to be notified of the changed credentials and
  // send a new access token request.
  base::RunLoop().RunUntilIdle();
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithError(
      GoogleServiceAuthError(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));

  // Check that the invalid token is returned from sync.
  ASSERT_EQ(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS,
            service()->GetAuthError().state());
  // Sync should pause.
  ASSERT_EQ(SyncService::TransportState::PAUSED,
            service()->GetTransportState());

  // Now emulate Chrome receiving a new, valid LST.
  identity_test_env()->SetRefreshTokenForPrimaryAccount();
  // Again, wait for SyncServiceImpl to be notified.
  base::RunLoop().RunUntilIdle();
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      "this one works", base::Time::Now() + base::Days(10));

  // Check that sync auth error state cleared.
  EXPECT_EQ(GoogleServiceAuthError::NONE, service()->GetAuthError().state());
  EXPECT_EQ(GoogleServiceAuthError::NONE, observer.auth_error().state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  service()->RemoveObserver(&observer);
}

// Verify that the disable sync flag disables sync.
TEST_F(SyncServiceImplTest, DisableSyncFlag) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kDisableSync);
  EXPECT_FALSE(IsSyncAllowedByFlag());
}

// Verify that no disable sync flag enables sync.
TEST_F(SyncServiceImplTest, NoDisableSyncFlag) {
  EXPECT_TRUE(IsSyncAllowedByFlag());
}

// Test that when SyncServiceImpl receives actionable error
// RESET_LOCAL_SYNC_DATA it restarts sync.
TEST_F(SyncServiceImplTest, ResetSyncData) {
  SignIn();
  CreateService();
  // Backend should get initialized two times: once during initialization and
  // once when handling actionable error.
  InitializeForNthSync();

  SyncProtocolError client_cmd;
  client_cmd.action = RESET_LOCAL_SYNC_DATA;
  service()->OnActionableProtocolError(client_cmd);
}

// Test that when SyncServiceImpl receives actionable error
// DISABLE_SYNC_ON_CLIENT it disables sync and signs out.
TEST_F(SyncServiceImplTest, DisableSyncOnClient) {
  SignIn();

  CreateService();

  InitializeForNthSync();

  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  ASSERT_EQ(0, get_controller(BOOKMARKS)->model()->clear_metadata_call_count());

#if BUILDFLAG(IS_CHROMEOS_ASH)
  ASSERT_FALSE(service()->IsSyncFeatureDisabledViaDashboard());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  EXPECT_CALL(
      *trusted_vault_client(),
      ClearLocalDataForAccount(Eq(identity_manager()->GetPrimaryAccountInfo(
          signin::ConsentLevel::kSync))));

  SyncProtocolError client_cmd;
  client_cmd.action = DISABLE_SYNC_ON_CLIENT;
  service()->OnActionableProtocolError(client_cmd);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Ash does not support signout.
  EXPECT_TRUE(
      identity_manager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  // Since ChromeOS doesn't support signout and so the account is still there
  // and available, Sync will restart in standalone transport mode.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_TRUE(service()->IsSyncFeatureDisabledViaDashboard());
#elif BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  // On iOS and Android, the primary account is cleared.
  EXPECT_FALSE(
      identity_manager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  EXPECT_EQ(SyncService::DisableReasonSet(
                {SyncService::DISABLE_REASON_NOT_SIGNED_IN}),
            service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            service()->GetTransportState());
  EXPECT_TRUE(service()->GetLastSyncedTimeForDebugging().is_null());
#else
  // On Desktop and Lacros, the sync consent is revoked, but the primary account
  // is left at ConsentLevel::kSignin. Sync will restart in standalone transport
  // mode.
  EXPECT_FALSE(
      identity_manager()->HasPrimaryAccount(signin::ConsentLevel::kSync));
  EXPECT_TRUE(
      identity_manager()->HasPrimaryAccount(signin::ConsentLevel::kSignin));
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  EXPECT_GT(get_controller(BOOKMARKS)->model()->clear_metadata_call_count(), 0);

  EXPECT_FALSE(service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(service()->IsSyncFeatureActive());
}

TEST_F(SyncServiceImplTest,
       DisableSyncOnClientLogsPassphraseTypeForNotMyBirthday) {
  const PassphraseType kPassphraseType = PassphraseType::kKeystorePassphrase;

  SignIn();
  CreateService();
  InitializeForNthSync();

  service()->GetEncryptionObserverForTest()->OnPassphraseTypeChanged(
      kPassphraseType, /*passphrase_time=*/base::Time());

  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  ASSERT_TRUE(service()->IsSyncFeatureEnabled());
  ASSERT_EQ(kPassphraseType, service()->GetUserSettings()->GetPassphraseType());

  SyncProtocolError client_cmd;
  client_cmd.action = DISABLE_SYNC_ON_CLIENT;
  client_cmd.error_type = NOT_MY_BIRTHDAY;

  base::HistogramTester histogram_tester;
  service()->OnActionableProtocolError(client_cmd);

  ASSERT_FALSE(service()->IsSyncFeatureEnabled());

  histogram_tester.ExpectUniqueSample(
      "Sync.PassphraseTypeUponNotMyBirthdayOrEncryptionObsolete",
      /*sample=*/kPassphraseType,
      /*expected_bucket_count=*/1);
}

TEST_F(SyncServiceImplTest,
       DisableSyncOnClientLogsPassphraseTypeForEncryptionObsolete) {
  const PassphraseType kPassphraseType = PassphraseType::kKeystorePassphrase;

  SignIn();
  CreateService();
  InitializeForNthSync();

  service()->GetEncryptionObserverForTest()->OnPassphraseTypeChanged(
      kPassphraseType, /*passphrase_time=*/base::Time());

  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  ASSERT_TRUE(service()->IsSyncFeatureEnabled());
  ASSERT_EQ(kPassphraseType, service()->GetUserSettings()->GetPassphraseType());

  SyncProtocolError client_cmd;
  client_cmd.action = DISABLE_SYNC_ON_CLIENT;
  client_cmd.error_type = ENCRYPTION_OBSOLETE;

  base::HistogramTester histogram_tester;
  service()->OnActionableProtocolError(client_cmd);

  ASSERT_FALSE(service()->IsSyncFeatureEnabled());

  histogram_tester.ExpectUniqueSample(
      "Sync.PassphraseTypeUponNotMyBirthdayOrEncryptionObsolete",
      /*sample=*/kPassphraseType,
      /*expected_bucket_count=*/1);
}

// Verify a that local sync mode isn't impacted by sync being disabled.
TEST_F(SyncServiceImplTest, LocalBackendUnimpactedByPolicy) {
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(false));
  CreateServiceWithLocalSyncBackend();
  InitializeForNthSync();
  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // The transport should continue active even if kSyncManaged becomes true.
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(true));

  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // Setting kSyncManaged back to false should also make no difference.
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(false));

  EXPECT_TRUE(service()->GetDisableReasons().Empty());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
}

// Test ConfigureDataTypeManagerReason on First and Nth start.
TEST_F(SyncServiceImplTest, ConfigureDataTypeManagerReason) {
  SignIn();

  // First sync.
  CreateService();
  InitializeForFirstSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_EQ(CONFIGURE_REASON_NEW_CLIENT,
            data_type_manager()->last_configure_reason_for_test());

  // Reconfiguration.
  // Trigger a reconfig by grabbing a SyncSetupInProgressHandle and immediately
  // releasing it again (via the temporary unique_ptr going away).
  service()->GetSetupInProgressHandle();
  EXPECT_EQ(CONFIGURE_REASON_RECONFIGURATION,
            data_type_manager()->last_configure_reason_for_test());
  ShutdownAndDeleteService();

  // Nth sync.
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_EQ(CONFIGURE_REASON_NEWLY_ENABLED_DATA_TYPE,
            data_type_manager()->last_configure_reason_for_test());

  // Reconfiguration.
  // Trigger a reconfig by grabbing a SyncSetupInProgressHandle and immediately
  // releasing it again (via the temporary unique_ptr going away).
  service()->GetSetupInProgressHandle();
  EXPECT_EQ(CONFIGURE_REASON_RECONFIGURATION,
            data_type_manager()->last_configure_reason_for_test());
  ShutdownAndDeleteService();
}

// Regression test for crbug.com/1043642, can be removed once
// SyncServiceImpl usages after shutdown are addressed.
TEST_F(SyncServiceImplTest, ShouldProvideDisableReasonsAfterShutdown) {
  SignIn();
  CreateService();
  InitializeForFirstSync();
  service()->Shutdown();
  EXPECT_FALSE(service()->GetDisableReasons().Empty());
}

TEST_F(SyncServiceImplTest, ShouldSendDataTypesToSyncInvalidationsService) {
  SignIn();
  CreateService(
      /*registered_types_and_transport_mode_support=*/
      {
          {BOOKMARKS, false},
          {DEVICE_INFO, true},
      });
  // Note: Even though NIGORI technically isn't registered, it should always be
  // considered part of the interested data types.
  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(AllOf(ContainsDataType(NIGORI),
                                           ContainsDataType(BOOKMARKS),
                                           ContainsDataType(DEVICE_INFO))));
  InitializeForNthSync();
  ASSERT_TRUE(service()->IsSyncFeatureActive());
  EXPECT_TRUE(engine()->started_handling_invalidations());
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(SyncServiceImplTest,
       ShouldSendDataTypesToSyncInvalidationsServiceInTransportMode) {
  SignIn();
  CreateService(
      /*registered_types_and_transport_mode_support=*/
      {
          {BOOKMARKS, false},
          {DEVICE_INFO, true},
      });

  // In this test, BOOKMARKS doesn't support transport mode, so it should *not*
  // be included.
  // Note: Even though NIGORI technically isn't registered, it should always be
  // considered part of the interested data types.
  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(AllOf(ContainsDataType(NIGORI),
                                           Not(ContainsDataType(BOOKMARKS)),
                                           ContainsDataType(DEVICE_INFO))));
  InitializeForFirstSync();

  ASSERT_FALSE(service()->IsSyncFeatureActive());
  EXPECT_TRUE(engine()->started_handling_invalidations());
}
#else
TEST_F(SyncServiceImplTest,
       ShouldSendDataTypesToSyncInvalidationsServiceInTransportModeAsh) {
  SignIn();
  CreateService(
      /*registered_types_and_transport_mode_support=*/
      {
          {BOOKMARKS, false},
          {DEVICE_INFO, true},
      });
  InitializeForFirstSync();

  // In this test, BOOKMARKS doesn't support transport mode, so it should *not*
  // be included.
  // Note: Even though NIGORI technically isn't registered, it should always be
  // considered part of the interested data types.
  // Note2: InitializeForFirstSync() issued a first SetInterestedDataTypes()
  // with sync-the-feature enabled, which we don't care about. That's why this
  // expectation is set afterwards.
  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(AllOf(ContainsDataType(NIGORI),
                                           Not(ContainsDataType(BOOKMARKS)),
                                           ContainsDataType(DEVICE_INFO))));

  // Sync-the-feature is normally enabled in Ash. Triggering a dashboard reset
  // is one way to achieve otherwise.
  SyncProtocolError client_cmd;
  client_cmd.action = DISABLE_SYNC_ON_CLIENT;
  service()->OnActionableProtocolError(client_cmd);
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(service()->IsSyncFeatureActive());
  EXPECT_TRUE(engine()->started_handling_invalidations());
}
#endif

TEST_F(SyncServiceImplTest, ShouldEnableAndDisableInvalidationsForSessions) {
  SignIn();
  CreateService({{SESSIONS, false}, {TYPED_URLS, false}});
  InitializeForNthSync();

  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(ContainsDataType(SESSIONS)));
  service()->SetInvalidationsForSessionsEnabled(true);
  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(Not(ContainsDataType(SESSIONS))));
  service()->SetInvalidationsForSessionsEnabled(false);
}

TEST_F(SyncServiceImplTest, ShouldNotSubscribeToProxyTypes) {
  SignIn();
  CreateService(
      /*registered_types_and_transport_mode_support=*/
      {
          {BOOKMARKS, false},
          {DEVICE_INFO, true},
      });
  get_controller(BOOKMARKS)
      ->model()
      ->EnableSkipEngineConnectionForActivationResponse();
  EXPECT_CALL(*sync_invalidations_service(),
              SetInterestedDataTypes(AllOf(ContainsDataType(DEVICE_INFO),
                                           Not(ContainsDataType(BOOKMARKS)))));
  InitializeForNthSync();
}

TEST_F(SyncServiceImplTest,
       ShouldActivateSyncInvalidationsServiceWhenSyncIsInitialized) {
  SignIn();
  CreateService();

  // Invalidations may start listening twice. The first one during
  // initialization, the second once everything is configured.
  EXPECT_CALL(*sync_invalidations_service(), StartListening())
      .Times(AtLeast(1));
  InitializeForFirstSync();
}

TEST_F(SyncServiceImplTest,
       ShouldNotStartListeningInvalidationsWhenLocalSyncEnabled) {
  CreateServiceWithLocalSyncBackend();
  EXPECT_CALL(*sync_invalidations_service(), StartListening()).Times(0);
  InitializeForFirstSync();
}

TEST_F(SyncServiceImplTest,
       ShouldNotStopListeningPermanentlyOnShutdownBrowserAndKeepData) {
  SignIn();
  CreateService();
  InitializeForFirstSync();
  EXPECT_CALL(*sync_invalidations_service(), StopListeningPermanently())
      .Times(0);
  ShutdownAndDeleteService();
}

TEST_F(SyncServiceImplTest,
       ShouldStopListeningPermanentlyOnDisableSyncAndClearData) {
  SignIn();
  CreateService();
  InitializeForFirstSync();
  EXPECT_CALL(*sync_invalidations_service(), StopListeningPermanently());
  service()->StopAndClear();
}

TEST_F(SyncServiceImplTest, ShouldCallStopUponResetEngineIfAlreadyShutDown) {
  base::test::ScopedFeatureList feature_list(
      syncer::kSyncAllowClearingMetadataWhenDataTypeIsStopped);

  // The intention here is to stop sync without clearing metadata by getting to
  // a sync paused state by simulating a credential rejection error.

  // Sign in and enable sync.
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());

  // At this point, the real SyncEngine would try to connect to the server, fail
  // (because it has no access token), and eventually call
  // OnConnectionStatusChange(CONNECTION_AUTH_ERROR). Since our fake SyncEngine
  // doesn't do any of this, call that explicitly here.
  service()->OnConnectionStatusChange(CONNECTION_AUTH_ERROR);

  base::RunLoop().RunUntilIdle();

  // Simulate the credentials getting locally rejected by the client by setting
  // the refresh token to a special invalid value.
  identity_test_env()->SetInvalidRefreshTokenForPrimaryAccount();

  // The Sync engine should have been shut down.
  ASSERT_FALSE(service()->IsEngineInitialized());
  ASSERT_EQ(SyncService::TransportState::PAUSED,
            service()->GetTransportState());

  EXPECT_EQ(0, get_controller(BOOKMARKS)->model()->clear_metadata_call_count());
  // Clearing metadata should work even if the engine is not running.
  service()->StopAndClear();
  EXPECT_EQ(1, get_controller(BOOKMARKS)->model()->clear_metadata_call_count());
}

TEST_F(SyncServiceImplTest, ShouldReturnErrorDownloadStatus) {
  SignIn();
  CreateService();
  InitializeForNthSync();

  data_type_manager()->OnSingleDataTypeWillStop(
      syncer::BOOKMARKS,
      SyncError(FROM_HERE, SyncError::ErrorType::DATATYPE_ERROR,
                "Data type failure", syncer::BOOKMARKS));
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kError);
}

TEST_F(SyncServiceImplTest, ShouldReturnErrorDownloadStatusWhenSyncDisabled) {
  base::HistogramTester histogram_tester;
  prefs()->SetManagedPref(prefs::internal::kSyncManaged, base::Value(true));
  SignIn();
  CreateService();
  InitializeForNthSync();

  // OnInvalidationStatusChanged() is used to only notify observers. This will
  // cause the histogram recorder to check data types status.
  service()->OnInvalidationStatusChanged();
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kError);
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime",
                                    /*expected_count=*/0);
}

TEST_F(SyncServiceImplTest, ShouldReturnWaitingDownloadStatus) {
  SignIn();
  CreateService();

  ASSERT_THAT(data_type_manager(), IsNull());

  bool met_configuring_data_type_manager = false;
  testing::NiceMock<MockSyncServiceObserver> mock_sync_service_observer;
  ON_CALL(mock_sync_service_observer, OnStateChanged)
      .WillByDefault(Invoke([this, &met_configuring_data_type_manager](
                                SyncService* service) {
        EXPECT_NE(service->GetDownloadStatusFor(syncer::BOOKMARKS),
                  SyncService::ModelTypeDownloadStatus::kError);
        if (!data_type_manager()) {
          return;
        }
        if (data_type_manager()->state() ==
            DataTypeManager::State::CONFIGURING) {
          met_configuring_data_type_manager = true;
          EXPECT_EQ(service->GetDownloadStatusFor(syncer::BOOKMARKS),
                    SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);
        }
      }));

  // Observers must be added after initialization has been started.
  InitializeForNthSync(false);
  ASSERT_THAT(engine(), IsNull());

  // GetDownloadStatusFor() must be called only after Initialize(), see
  // SyncServiceImpl::Initialize().
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);

  service()->AddObserver(&mock_sync_service_observer);
  base::RunLoop().RunUntilIdle();
  SetInvalidationsEnabled();

  EXPECT_TRUE(met_configuring_data_type_manager);
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kUpToDate);
  service()->RemoveObserver(&mock_sync_service_observer);
}

TEST_F(SyncServiceImplTest, ShouldReturnErrorWhenDataTypeDisabled) {
  base::HistogramTester histogram_tester;
  SignIn();
  CreateService();
  InitializeForNthSync(/*run_until_idle=*/false);

  UserSelectableTypeSet enabled_types =
      service()->GetUserSettings()->GetSelectedTypes();
  enabled_types.Remove(UserSelectableType::kBookmarks);
  service()->GetUserSettings()->SetSelectedTypes(/*sync_everything=*/false,
                                                 enabled_types);

  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kError);

  // Finish initialization and double check that the status hasn't changed.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kError);

  SetInvalidationsEnabled();
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime.BOOKMARK",
                                    /*expected_count=*/0);
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime",
                                    /*expected_count=*/1);
}

TEST_F(SyncServiceImplTest, ShouldWaitUntilNoInvalidations) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  SetInvalidationsEnabled();

  SyncStatus status = engine()->GetDetailedStatus();
  status.invalidated_data_types.Put(BOOKMARKS);
  engine()->SetDetailedStatus(status);

  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::DEVICE_INFO),
            SyncService::ModelTypeDownloadStatus::kUpToDate);
}

TEST_F(SyncServiceImplTest, ShouldWaitForInitializedInvalidations) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);

  SetInvalidationsEnabled();
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kUpToDate);
}

TEST_F(SyncServiceImplTest, ShouldWaitForPollRequest) {
  base::HistogramTester histogram_tester;
  SignIn();
  CreateService();
  InitializeForNthSync();
  SetInvalidationsEnabled();
  ASSERT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kUpToDate);

  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime.BOOKMARK",
                                    /*expected_count=*/1);
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime",
                                    /*expected_count=*/1);

  // OnInvalidationStatusChanged() is used to only notify observers, this is
  // required for metrics since they are calculated only when SyncService state
  // changes.
  engine()->SetPollIntervalElapsed(true);
  service()->OnInvalidationStatusChanged();
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);

  engine()->SetPollIntervalElapsed(false);
  service()->OnInvalidationStatusChanged();
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kUpToDate);

  // The histograms should be recorded only once.
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime.BOOKMARK",
                                    /*expected_count=*/1);
  histogram_tester.ExpectTotalCount("Sync.ModelTypeUpToDateTime",
                                    /*expected_count=*/1);
}

TEST_F(SyncServiceImplTest, ShouldReturnErrorOnSyncPaused) {
  SignIn();
  CreateService();
  InitializeForNthSync();
  ASSERT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kWaitingForUpdates);

  // Mimic entering Sync paused state.
  identity_test_env()->SetInvalidRefreshTokenForPrimaryAccount();
  ASSERT_EQ(SyncService::TransportState::PAUSED,
            service()->GetTransportState());

  // Expect the error status when Sync is paused.
  EXPECT_EQ(service()->GetDownloadStatusFor(syncer::BOOKMARKS),
            SyncService::ModelTypeDownloadStatus::kError);
}

TEST_F(
    SyncServiceImplTest,
    GetTypesWithPendingDownloadForInitialSyncDuringFirstSyncInTransportMode) {
  component_factory()->AllowFakeEngineInitCompletion(false);
  CreateService();
  InitializeForFirstSync();

#if BUILDFLAG(IS_IOS)
  // Outside iOS, transport mode considers all types as enabled by default. On
  // iOS, for BOOKMARKS to be listed as preferred, an explicit API call is
  // needed.
  service()->GetUserSettings()->SetBookmarksAndReadingListAccountStorageOptIn(
      true);
#endif  // BUILDFLAG(IS_IOS)

  identity_test_env()->MakePrimaryAccountAvailable(
      kTestUser, signin::ConsentLevel::kSignin);

  ASSERT_EQ(SyncService::TransportState::START_DEFERRED,
            service()->GetTransportState());

  // START_DEFERRED is very short-lived upon sign-in, so it doesn't matter
  // much what the API returns (added here for documentation purposes).
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());

  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(SyncService::TransportState::INITIALIZING,
            service()->GetTransportState());

  // During first-sync INITIALIZING, all preferred datatypes are listed, which
  // in this test fixture means NIGORI, BOOKMARKS and DEVICE_INFO.
  EXPECT_EQ(ModelTypeSet({NIGORI, BOOKMARKS, DEVICE_INFO}),
            service()->GetTypesWithPendingDownloadForInitialSync());

  // Once fully initialized, it is delegated to DataTypeManager.
  engine()->TriggerInitializationCompletion(/*success=*/true);
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());
}

TEST_F(SyncServiceImplTest,
       GetTypesWithPendingDownloadForInitialSyncDuringFirstSync) {
  component_factory()->AllowFakeEngineInitCompletion(false);
  CreateService();
  InitializeForFirstSync();
  SignIn();

  service()->GetUserSettings()->SetInitialSyncFeatureSetupComplete(
      syncer::SyncFirstSetupCompleteSource::BASIC_FLOW);

  ASSERT_EQ(SyncService::TransportState::START_DEFERRED,
            service()->GetTransportState());

  // START_DEFERRED is very short-lived upon sign-in, so it doesn't matter
  // much what the API returns (added here for documentation purposes).
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());

  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(SyncService::TransportState::INITIALIZING,
            service()->GetTransportState());

  // During first-sync INITIALIZING, all preferred datatypes are listed, which
  // in this test fixture means NIGORI, BOOKMARKS and DEVICE_INFO.
  EXPECT_EQ(ModelTypeSet({NIGORI, BOOKMARKS, DEVICE_INFO}),
            service()->GetTypesWithPendingDownloadForInitialSync());

  // Once fully initialized, it is delegated to DataTypeManager.
  engine()->TriggerInitializationCompletion(/*success=*/true);
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());
}

TEST_F(SyncServiceImplTest,
       GetTypesWithPendingDownloadForInitialSyncDuringNthSync) {
  component_factory()->AllowFakeEngineInitCompletion(false);
  SignIn();
  CreateService();
  InitializeForNthSync(/*run_until_idle=*/false);

  ASSERT_EQ(SyncService::TransportState::START_DEFERRED,
            service()->GetTransportState());

  // During non-first-sync initialization, usually during profile startup,
  // SyncService doesn't actually know which datatypes are pending download, so
  // it defaults to returning an empty set.
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());

  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(SyncService::TransportState::INITIALIZING,
            service()->GetTransportState());

  // Same as above.
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());

  // Once fully initialized, it is delegated to DataTypeManager.
  engine()->TriggerInitializationCompletion(/*success=*/true);
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            service()->GetTransportState());
  EXPECT_EQ(ModelTypeSet(),
            service()->GetTypesWithPendingDownloadForInitialSync());
}

}  // namespace
}  // namespace syncer

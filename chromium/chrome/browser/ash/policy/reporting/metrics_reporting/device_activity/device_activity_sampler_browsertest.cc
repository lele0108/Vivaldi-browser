// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ash/login/lock/screen_locker_tester.h"
#include "chrome/browser/ash/login/test/cryptohome_mixin.h"
#include "chrome/browser/ash/policy/affiliation/affiliation_mixin.h"
#include "chrome/browser/ash/policy/affiliation/affiliation_test_helper.h"
#include "chrome/browser/ash/policy/core/device_policy_cros_browser_test.h"
#include "chrome/browser/ash/settings/scoped_testing_cros_settings.h"
#include "chrome/browser/ash/settings/stub_cros_settings_provider.h"
#include "chrome/browser/chromeos/reporting/metric_default_utils.h"
#include "chrome/browser/policy/dm_token_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chromeos/ash/components/settings/cros_settings_names.h"
#include "chromeos/dbus/missive/missive_client_test_observer.h"
#include "components/reporting/proto/synced/metric_data.pb.h"
#include "components/reporting/proto/synced/record.pb.h"
#include "components/reporting/proto/synced/record_constants.pb.h"
#include "components/reporting/util/mock_clock.h"
#include "components/session_manager/core/session_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_launcher.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/user_activity/user_activity_detector.h"

using ::testing::Eq;
using ::testing::StrEq;

namespace reporting {
namespace {

// Test DM token used to associate reported events.
constexpr char kDMToken[] = "token";

// Device idle state threshold.
constexpr base::TimeDelta kIdleStateThreshold = base::Minutes(5);

// Assert device activity status telemetry data in a record with relevant DM
// token and returns the underlying `MetricData` object.
const MetricData AssertDeviceActivityStatusTelemetryData(Priority priority,
                                                         const Record& record) {
  EXPECT_THAT(priority, Eq(Priority::MANUAL_BATCH));
  EXPECT_THAT(record.destination(), Eq(Destination::TELEMETRY_METRIC));

  MetricData record_data;
  EXPECT_TRUE(record_data.ParseFromString(record.data()));
  EXPECT_TRUE(record_data.has_timestamp_ms());
  EXPECT_TRUE(record.has_dm_token());
  EXPECT_THAT(record.dm_token(), StrEq(kDMToken));
  return record_data;
}

// Returns true if the record includes user status telemetry. False otherwise.
bool IsUserStatusTelemetry(const Record& record) {
  MetricData record_data;
  return record_data.ParseFromString(record.data()) &&
         record_data.has_telemetry_data() &&
         record_data.telemetry_data().has_user_status_telemetry();
}

// Browser test that validates device activity status telemetry reported by the
// `DeviceActivityTelemetrySampler`. Inheriting from
// `DevicePolicyCrosBrowserTest` enables use of `AffiliationMixin` for setting
// up profile/device affiliation. Only available in Ash.
class DeviceActivitySamplerBrowserTest
    : public ::policy::DevicePolicyCrosBrowserTest {
 protected:
  DeviceActivitySamplerBrowserTest() {
    // Initialize the MockClock.
    test::MockClock::Get();
    crypto_home_mixin_.MarkUserAsExisting(affiliation_mixin_.account_id());
    ::policy::SetDMTokenForTesting(
        ::policy::DMToken::CreateValidToken(kDMToken));
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ::policy::AffiliationTestHelper::AppendCommandLineSwitchesForLoginManager(
        command_line);
    ::policy::DevicePolicyCrosBrowserTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    ::policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    if (::content::IsPreTest()) {
      // Preliminary setup - set up affiliated user.
      ::policy::AffiliationTestHelper::PreLoginUser(
          affiliation_mixin_.account_id());
      return;
    }

    // Login as affiliated user otherwise.
    ::policy::AffiliationTestHelper::LoginUser(affiliation_mixin_.account_id());
  }

  void SetPolicyEnabled(bool is_enabled) {
    scoped_testing_cros_settings_.device_settings()->SetBoolean(
        ::ash::kDeviceActivityHeartbeatEnabled, is_enabled);
  }

 private:
  ::policy::DevicePolicyCrosTestHelper test_helper_;
  ::policy::AffiliationMixin affiliation_mixin_{&mixin_host_, &test_helper_};
  ::ash::CryptohomeMixin crypto_home_mixin_{&mixin_host_};
  ::ash::ScopedTestingCrosSettings scoped_testing_cros_settings_;
};

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest,
                       PRE_ReportLockedActivityState) {
  // Dummy case that sets up the affiliated user through SetUpOnMainThread
  // PRE-condition.
}

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest,
                       ReportLockedActivityState) {
  SetPolicyEnabled(true);

  // Simulate locked activity for the current session.
  DCHECK(::session_manager::SessionManager::Get());
  ::ash::ScreenLockerTester().Lock();

  // Force telemetry collection by advancing the timer and verify data that is
  // being enqueued via ERP.
  ::chromeos::MissiveClientTestObserver missive_observer(
      base::BindRepeating(&IsUserStatusTelemetry));
  test::MockClock::Get().Advance(
      metrics::kDefaultDeviceActivityHeartbeatCollectionRate);
  const auto [priority, record] = missive_observer.GetNextEnqueuedRecord();
  const auto metric_data =
      AssertDeviceActivityStatusTelemetryData(priority, record);
  EXPECT_THAT(metric_data.telemetry_data()
                  .user_status_telemetry()
                  .device_activity_state(),
              Eq(UserStatusTelemetry::LOCKED));
}

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest, PRE_ReportIdleState) {
  // Dummy case that sets up the affiliated user through SetUpOnMainThread
  // PRE-condition.
}

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest, ReportIdleState) {
  SetPolicyEnabled(true);

  // Simulate idle activity by recording current timestamp for last activity and
  // advancing timer beyond the idle threshold.
  ::ui::UserActivityDetector::Get()->set_last_activity_time_for_test(
      base::TimeTicks::Now());
  test::MockClock::Get().Advance(kIdleStateThreshold);

  // Force telemetry collection by advancing the timer and verify data that is
  // being enqueued via ERP.
  ::chromeos::MissiveClientTestObserver missive_observer(
      base::BindRepeating(&IsUserStatusTelemetry));
  test::MockClock::Get().Advance(
      metrics::kDefaultDeviceActivityHeartbeatCollectionRate);
  const auto [priority, record] = missive_observer.GetNextEnqueuedRecord();
  const auto metric_data =
      AssertDeviceActivityStatusTelemetryData(priority, record);
  EXPECT_THAT(metric_data.telemetry_data()
                  .user_status_telemetry()
                  .device_activity_state(),
              Eq(UserStatusTelemetry::IDLE));
}

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest,
                       PRE_ReportActiveState) {
  // Dummy case that sets up the affiliated user through SetUpOnMainThread
  // PRE-condition.
}

IN_PROC_BROWSER_TEST_F(DeviceActivitySamplerBrowserTest, ReportActiveState) {
  SetPolicyEnabled(true);

  // Simulate activity by recording last activity that does not exceed the idle
  // threshold. We will have to adjust this based on the collection frequency so
  // the device is still considered active when the metric is reported.
  const base::TimeTicks last_activity_time =
      base::TimeTicks::Now() +
      metrics::kDefaultDeviceActivityHeartbeatCollectionRate;
  ::ui::UserActivityDetector::Get()->set_last_activity_time_for_test(
      last_activity_time);

  // Force telemetry collection by advancing the timer and verify data that is
  // being enqueued via ERP.
  ::chromeos::MissiveClientTestObserver missive_observer(
      base::BindRepeating(&IsUserStatusTelemetry));
  test::MockClock::Get().Advance(
      metrics::kDefaultDeviceActivityHeartbeatCollectionRate);
  const auto [priority, record] = missive_observer.GetNextEnqueuedRecord();
  const auto metric_data =
      AssertDeviceActivityStatusTelemetryData(priority, record);
  EXPECT_THAT(metric_data.telemetry_data()
                  .user_status_telemetry()
                  .device_activity_state(),
              Eq(UserStatusTelemetry::ACTIVE));
}

}  // namespace
}  // namespace reporting

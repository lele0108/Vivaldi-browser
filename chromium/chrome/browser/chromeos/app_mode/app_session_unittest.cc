// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "ash/constants/ash_switches.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/values_util.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/chromeos/app_mode/app_session.h"
#include "chrome/browser/chromeos/app_mode/app_session_browser_window_handler.h"
#include "chrome/browser/chromeos/app_mode/app_session_metrics_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/scoped_testing_local_state.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/dbus/power/fake_power_manager_client.h"
#include "content/public/common/webplugininfo.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/geometry/rect.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/accelerators/accelerator_controller_impl.h"
#include "ash/public/cpp/session/session_types.h"
#include "ash/public/cpp/test/test_new_window_delegate.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/test/ash_test_helper.h"
#include "ash/wm/overview/overview_controller.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(ENABLE_PLUGINS)
#include "chrome/browser/chromeos/app_mode/kiosk_session_plugin_handler_delegate.h"
#include "content/public/browser/plugin_service.h"
#endif  // BUILDFLAG(ENABLE_PLUGINS)

namespace chromeos {

namespace {

using ::chromeos::FakePowerManagerClient;

constexpr char kTestAppId[] = "aaaabbbbaaaabbbbaaaabbbbaaaabbbb";
constexpr char kTestWebAppName1[] = "test_web_app_name1";
constexpr char kTestWebAppName2[] = "test_web_app_name2";

#if BUILDFLAG(ENABLE_PLUGINS)
constexpr char16_t kPepperPluginName1[] = u"pepper_plugin_name1";
constexpr char16_t kPepperPluginName2[] = u"pepper_plugin_name2";
constexpr char16_t kBrowserPluginName[] = u"browser_plugin_name";
constexpr char kPepperPluginFilePath1[] = "/path/to/pepper_plugin1";
constexpr char kPepperPluginFilePath2[] = "/path/to/pepper_plugin2";
constexpr char kBrowserPluginFilePath[] = "/path/to/browser_plugin";
constexpr char kUnregisteredPluginFilePath[] = "/path/to/unregistered_plugin";
#endif  // BUILDFLAG(ENABLE_PLUGINS)

// A test browser window that can toggle fullscreen state.
class FullscreenTestBrowserWindow : public TestBrowserWindow,
                                    ExclusiveAccessContext {
 public:
  explicit FullscreenTestBrowserWindow(TestingProfile* profile,
                                       bool fullscreen = false)
      : fullscreen_(fullscreen), profile_(profile) {}

  FullscreenTestBrowserWindow(const FullscreenTestBrowserWindow&) = delete;
  FullscreenTestBrowserWindow& operator=(const FullscreenTestBrowserWindow&) =
      delete;

  ~FullscreenTestBrowserWindow() override = default;

  // TestBrowserWindow:
  bool ShouldHideUIForFullscreen() const override { return fullscreen_; }
  bool IsFullscreen() const override { return fullscreen_; }
  void EnterFullscreen(const GURL& url,
                       ExclusiveAccessBubbleType type,
                       int64_t display_id) override {
    fullscreen_ = true;
  }
  void ExitFullscreen() override { fullscreen_ = false; }
  bool IsToolbarShowing() const override { return false; }
  bool IsLocationBarVisible() const override { return true; }

  ExclusiveAccessContext* GetExclusiveAccessContext() override { return this; }

  // ExclusiveAccessContext:
  Profile* GetProfile() override { return profile_; }
  content::WebContents* GetActiveWebContents() override {
    NOTIMPLEMENTED();
    return nullptr;
  }
  void UpdateExclusiveAccessExitBubbleContent(
      const GURL& url,
      ExclusiveAccessBubbleType bubble_type,
      ExclusiveAccessBubbleHideCallback bubble_first_hide_callback,
      bool notify_download,
      bool force_update) override {}
  bool IsExclusiveAccessBubbleDisplayed() const override { return false; }
  void OnExclusiveAccessUserInput() override {}
  bool CanUserExitFullscreen() const override { return true; }

 private:
  bool fullscreen_ = false;
  raw_ptr<TestingProfile> profile_;
};

bool IsBrowserFullscreen(Browser& browser) {
  return browser.exclusive_access_manager()
      ->fullscreen_controller()
      ->IsFullscreenForBrowser();
}

std::unique_ptr<Browser> CreateBrowserWithFullscreenTestWindowForParams(
    Browser::CreateParams params,
    TestingProfile* profile,
    bool is_main_browser = false) {
  // The main browser window for the kiosk is always fullscreen in the
  // prodaction.
  FullscreenTestBrowserWindow* window =
      new FullscreenTestBrowserWindow(profile, /*fullscreen=*/is_main_browser);
  new TestBrowserWindowOwner(window);
  params.window = window;
  return base::WrapUnique<Browser>(Browser::Create(params));
}

void EmulateDeviceReboot() {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      ash::switches::kFirstExecAfterBoot);
}

struct KioskSessionRestartTestCase {
  std::string test_name;
  bool run_with_reboot = false;
};

struct KioskSessionPowerManagerRequestRestartTestCase {
  power_manager::RequestRestartReason power_manager_reason;
  KioskSessionRestartReason restart_reason;
};

void CheckSessionRestartReasonHistogramDependingOnRebootStatus(
    bool run_with_reboot,
    const KioskSessionRestartReason& reasonWithoutReboot,
    const KioskSessionRestartReason& reasonWithReboot,
    const base::HistogramTester* histogram) {
  if (run_with_reboot) {
    histogram->ExpectBucketCount(kKioskSessionRestartReasonHistogram,
                                 reasonWithReboot, 1);
  } else {
    histogram->ExpectBucketCount(kKioskSessionRestartReasonHistogram,
                                 reasonWithoutReboot, 1);
  }
  histogram->ExpectTotalCount(kKioskSessionRestartReasonHistogram, 1);
}

}  // namespace

// TODO(b/271336749): split app_session_unittest.cc file into smaller test
// files.
template <typename AppSessionParamType = KioskSessionRestartTestCase>
class AppSessionBaseTest
    : public ::testing::TestWithParam<AppSessionParamType> {
 public:
  explicit AppSessionBaseTest(
      base::test::TaskEnvironment::TimeSource time_source =
          base::test::TaskEnvironment::TimeSource::DEFAULT)
      : task_environment_{time_source},
        local_state_(std::make_unique<ScopedTestingLocalState>(
            TestingBrowserProcess::GetGlobal())) {}

  AppSessionBaseTest(const AppSessionBaseTest&) = delete;
  AppSessionBaseTest& operator=(const AppSessionBaseTest&) = delete;

  static void SetUpTestSuite() {
    chromeos::PowerManagerClient::InitializeFake();
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
#if BUILDFLAG(IS_CHROMEOS_ASH)
    ash_test_helper_.SetUp(ash::AshTestHelper::InitParams());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  }

  static void TearDownTestSuite() { chromeos::PowerManagerClient::Shutdown(); }

  TestingPrefServiceSimple* local_state() { return local_state_->Get(); }

  TestingProfile* profile() { return &profile_; }

  base::HistogramTester* histogram() { return &histogram_; }

  base::test::TaskEnvironment* task_environment() { return &task_environment_; }

  std::unique_ptr<Browser> CreateBrowserWithTestWindow() {
    return CreateBrowserWithFullscreenTestWindowForParams(
        Browser::CreateParams(profile(), true), profile());
  }

  std::unique_ptr<Browser> CreateBrowserForWebApp(
      const std::string& web_app_name,
      absl::optional<Browser::Type> browser_type = absl::nullopt) {
    Browser::CreateParams params = Browser::CreateParams::CreateForAppPopup(
        /*app_name=*/web_app_name, /*trusted_source=*/true,
        /*window_bounds=*/gfx::Rect(), /*profile=*/profile(),
        /*user_gesture=*/true);
    if (browser_type.has_value()) {
      params.type = browser_type.value();
    }
    return CreateBrowserWithFullscreenTestWindowForParams(params, profile());
  }

  // Simulate starting a web kiosk session.
  void StartWebKioskSession(
      const std::string& web_app_name = kTestWebAppName1) {
    // Create the main kiosk browser window, which is normally auto-created when
    // a web kiosk session starts.
    web_kiosk_main_browser_ = CreateBrowserWithFullscreenTestWindowForParams(
        Browser::CreateParams::CreateForApp(
            /*app_name=*/web_app_name, /*trusted_source=*/true,
            /*window_bounds=*/gfx::Rect(), /*profile=*/profile(),
            /*user_gesture=*/true),
        profile(), /*is_main_browser=*/true);

    app_session_ = AppSession::CreateForTesting(
        profile(), base::DoNothing(), local_state(), {crash_path().value()});
    app_session_->InitForWebKiosk(web_app_name);

    task_environment_.RunUntilIdle();
  }

  // Simulate starting a chrome app kiosk session.
  void StartChromeAppKioskSession() {
    app_session_ = std::make_unique<AppSession>(profile(), base::DoNothing(),
                                                local_state());
    app_session_->InitForChromeAppKiosk(kTestAppId);
  }

  // Waits until |app_session_| handles creation of |new_browser_window| and
  // returns whether |new_browser_window| was asked to close. In this case we
  // will also ensure that |new_browser_window| was automatically closed.
  bool ShouldBrowserBeClosedByAppSessionBrowserHander(
      BrowserWindow* new_browser_window) {
    bool already_closed = false;
    static_cast<TestBrowserWindow*>(new_browser_window)
        ->SetCloseCallback(base::BindLambdaForTesting(
            [&already_closed]() { already_closed = true; }));

    // Wait until the browser is handled by |app_session_|.
    base::RunLoop handler_loop;
    bool result = false;
    app_session_->SetOnHandleBrowserCallbackForTesting(
        base::BindLambdaForTesting([&handler_loop, &result](bool is_closing) {
          result = is_closing;
          handler_loop.Quit();
        }));
    handler_loop.Run();

    if (result) {
      EXPECT_TRUE(already_closed);
    }

    return result;
  }

  void CloseMainBrowser() {
    // Close the main browser window.
    web_kiosk_main_browser_.reset();
  }

  bool IsMainBrowserFullscreen() {
    return IsBrowserFullscreen(*web_kiosk_main_browser_);
  }

  bool IsSessionShuttingDown() const {
    return app_session_->is_shutting_down();
  }

  void ResetAppSession() { app_session_.reset(); }

  PrefService* GetPrefs() { return profile()->GetPrefs(); }

  KioskSessionPluginHandlerDelegate* GetPluginHandlerDelegate() {
    return app_session_->GetPluginHandlerDelegateForTesting();
  }

  base::FilePath crash_path() const { return temp_dir_.GetPath(); }

 private:
  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<ScopedTestingLocalState> local_state_;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  ash::AshTestHelper ash_test_helper_;
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Must outlive |app_session_|.
  TestingProfile profile_;
  // Main browser window created when launching a web kiosk app.
  // Could be nullptr if |StartWebKioskSession| function was not called.
  std::unique_ptr<Browser> web_kiosk_main_browser_;
  base::HistogramTester histogram_;
  std::unique_ptr<AppSession> app_session_;
};

using AppSessionTest = AppSessionBaseTest<>;
using AppSessionRestartReasonTest =
    AppSessionBaseTest<KioskSessionRestartTestCase>;

TEST_F(AppSessionTest, WebKioskTracksBrowserCreation) {
  {
    base::Value::Dict value;
    value.Set(kKioskSessionStartTime, base::TimeToValue(base::Time::Now()));

    local_state()->SetDict(prefs::kKioskMetrics, std::move(value));
  }

  StartWebKioskSession();
  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kWebStarted, 1);
  histogram()->ExpectTotalCount(kKioskSessionCountPerDayHistogram, 1);

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserWithTestWindow()->window()));

  // The main browser window still exists, the kiosk session should not
  // shutdown.
  EXPECT_FALSE(IsSessionShuttingDown());
  // Opening a new browser should not be counted as a new session.
  histogram()->ExpectTotalCount(kKioskSessionCountPerDayHistogram, 1);

  CloseMainBrowser();
  EXPECT_TRUE(IsSessionShuttingDown());

  const base::Value::Dict& dict = local_state()->GetDict(prefs::kKioskMetrics);
  const base::Value::List* sessions_list =
      dict.FindList(kKioskSessionLastDayList);
  ASSERT_TRUE(sessions_list);
  EXPECT_EQ(1u, sessions_list->size());

  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kStopped, 1);
  EXPECT_EQ(2u, histogram()->GetAllSamples(kKioskSessionStateHistogram).size());

  histogram()->ExpectTotalCount(kKioskSessionDurationNormalHistogram, 1);
  histogram()->ExpectTotalCount(kKioskSessionDurationInDaysNormalHistogram, 0);
}

TEST_F(AppSessionTest, ChromeAppKioskSessionState) {
  StartChromeAppKioskSession();
  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kStarted, 1);
  histogram()->ExpectTotalCount(kKioskSessionCountPerDayHistogram, 1);
}

TEST_F(AppSessionTest, ChromeAppKioskTracksBrowserCreation) {
  StartChromeAppKioskSession();

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserWithTestWindow()->window()));
  // Closing the browser should not shutdown the ChromeApp kiosk session.
  EXPECT_FALSE(IsSessionShuttingDown());
  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);

  const base::Value::Dict& dict = local_state()->GetDict(prefs::kKioskMetrics);
  const base::Value::List* sessions_list =
      dict.FindList(kKioskSessionLastDayList);
  ASSERT_TRUE(sessions_list);
  EXPECT_EQ(1u, sessions_list->size());

  // Emulate exiting kiosk session.
  ResetAppSession();

  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kStopped, 1);
  EXPECT_EQ(2u, histogram()->GetAllSamples(kKioskSessionStateHistogram).size());

  histogram()->ExpectTotalCount(kKioskSessionDurationNormalHistogram, 1);
  histogram()->ExpectTotalCount(kKioskSessionDurationInDaysNormalHistogram, 0);
}

// Check that sessions list in local_state contains only sessions within the
// last 24h.
TEST_F(AppSessionTest, WebKioskLastDaySessions) {
  // Setup local_state with 5 more kiosk sessions happened prior to the current
  // one: {now, 2,3,4,5 days ago}
  {
    base::Value::List session_list;
    session_list.Append(base::TimeToValue(base::Time::Now()));

    const size_t kMaxDays = 4;
    for (size_t i = 0; i < kMaxDays; i++) {
      session_list.Append(
          base::TimeToValue(base::Time::Now() - base::Days(i + 2)));
    }

    base::Value::Dict value;
    value.Set(kKioskSessionLastDayList, std::move(session_list));
    value.Set(kKioskSessionStartTime,
              base::TimeToValue(base::Time::Now() -
                                2 * kKioskSessionDurationHistogramLimit));

    local_state()->SetDict(prefs::kKioskMetrics, std::move(value));
  }

  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      ash::switches::kLoginUser, "fake-user");

  base::FilePath crash_file;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(crash_path(), &crash_file));

  StartWebKioskSession();
  // We set |kKioskSessionStartTime| for previous session and did not clear them
  // up, so it emulates previous session crashes.
  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kRestored, 1);
  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kCrashed, 1);
  histogram()->ExpectTotalCount(kKioskSessionDurationCrashedHistogram, 1);
  histogram()->ExpectTotalCount(kKioskSessionDurationInDaysCrashedHistogram, 1);
  histogram()->ExpectTotalCount(kKioskSessionCountPerDayHistogram, 1);

  CloseMainBrowser();
  EXPECT_TRUE(IsSessionShuttingDown());

  const base::Value::Dict& dict = local_state()->GetDict(prefs::kKioskMetrics);
  const base::Value::List* sessions_list =
      dict.FindList(kKioskSessionLastDayList);
  ASSERT_TRUE(sessions_list);
  // There should be only two kiosk sessions on the list:
  // the one that happened right before the current one and the current one.
  EXPECT_EQ(2u, sessions_list->size());
  for (const auto& time : *sessions_list) {
    EXPECT_LE(base::Time::Now() - base::ValueToTime(time).value(),
              base::Days(1));
  }

  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kStopped, 1);
  EXPECT_EQ(3u, histogram()->GetAllSamples(kKioskSessionStateHistogram).size());
  histogram()->ExpectTotalCount(kKioskSessionDurationNormalHistogram, 1);
  histogram()->ExpectTotalCount(kKioskSessionDurationInDaysNormalHistogram, 0);
}

TEST_F(AppSessionTest, DoNotOpenSecondBrowserInWebKiosk) {
  StartWebKioskSession(kTestWebAppName1);

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName1)->window()));
}

TEST_F(AppSessionTest, OpenSecondBrowserInWebKioskIfAllowed) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession(kTestWebAppName1);

  EXPECT_FALSE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName1)->window()));
}

TEST_F(AppSessionTest, EnsureSecondBrowserIsFullscreenInWebKiosk) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession(kTestWebAppName1);
  EXPECT_TRUE(IsMainBrowserFullscreen());

  auto second_browser = CreateBrowserForWebApp(kTestWebAppName1);
  ShouldBrowserBeClosedByAppSessionBrowserHander(second_browser->window());

  EXPECT_TRUE(IsBrowserFullscreen(*second_browser));
}

TEST_F(AppSessionTest, DoNotOpenSecondBrowserInWebKioskIfTypeIsNotAppPopup) {
  const std::vector<Browser::Type> not_app_popup_browser_types = {
    Browser::Type::TYPE_NORMAL,
    Browser::Type::TYPE_POPUP,
    Browser::Type::TYPE_APP,
    Browser::Type::TYPE_DEVTOOLS,
#if BUILDFLAG(IS_CHROMEOS_ASH)
    Browser::Type::TYPE_CUSTOM_TAB,
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    Browser::Type::TYPE_PICTURE_IN_PICTURE,
  };

  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession(kTestWebAppName1);

  for (auto browser_type : not_app_popup_browser_types) {
    EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
        CreateBrowserForWebApp(kTestWebAppName1, browser_type)->window()));
  }
}

TEST_F(AppSessionTest, DoNotOpenSecondBrowserInWebKioskWithEmptyWebAppName) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession();

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserWithTestWindow()->window()));
}

TEST_F(AppSessionTest,
       DoNotOpenSecondBrowserInWebKioskWithDifferentWebAppName) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession(kTestWebAppName1);

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName2)->window()));
}

TEST_F(AppSessionTest, DoNotOpenSecondBrowserInChromeAppKiosk) {
  // This flag allows opening new windows only for the web kiosk session. For
  // chrome app kiosk we still should block all new browsers.
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartChromeAppKioskSession();

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName2)->window()));
}

TEST_F(AppSessionTest, NewOpenedRegularBrowserMetrics) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession(kTestWebAppName1);

  ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName1)->window());

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kOpenedRegularBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);
}

TEST_F(AppSessionTest, NewClosedRegularBrowserMetrics) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, false);
  StartWebKioskSession(kTestWebAppName1);

  ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateBrowserForWebApp(kTestWebAppName1)->window());

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);
}

TEST_F(AppSessionTest, DoNotExitWebKioskSessionWhenSecondBrowserIsOpened) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession();

  auto second_browser = CreateBrowserForWebApp(kTestWebAppName1);
  EXPECT_FALSE(
      ShouldBrowserBeClosedByAppSessionBrowserHander(second_browser->window()));

  CloseMainBrowser();
  EXPECT_FALSE(IsSessionShuttingDown());

  second_browser.reset();
  // Exit kioks session when the last browser is closed.
  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_F(AppSessionTest, InitialBrowserShouldBeHandledAsRegularBrowser) {
  GetPrefs()->SetBoolean(prefs::kNewWindowsInKioskAllowed, true);
  StartWebKioskSession();

  auto second_browser = CreateBrowserForWebApp(kTestWebAppName1);
  EXPECT_FALSE(
      ShouldBrowserBeClosedByAppSessionBrowserHander(second_browser->window()));

  second_browser.reset();
  EXPECT_FALSE(IsSessionShuttingDown());

  CloseMainBrowser();
  // Exit kioks session when the last browser is closed.
  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_P(AppSessionRestartReasonTest, StoppedMetric) {
  const KioskSessionRestartTestCase& test_config = GetParam();
  StartWebKioskSession();
  // Emulate exiting the kiosk session.
  CloseMainBrowser();
  EXPECT_TRUE(IsSessionShuttingDown());
  if (test_config.run_with_reboot) {
    EmulateDeviceReboot();
  }
  histogram()->ExpectTotalCount(kKioskSessionRestartReasonHistogram, 0);

  StartWebKioskSession();

  if (test_config.run_with_reboot) {
    histogram()->ExpectBucketCount(
        kKioskSessionRestartReasonHistogram,
        KioskSessionRestartReason::kStoppedWithReboot, 1);
  } else {
    histogram()->ExpectBucketCount(kKioskSessionRestartReasonHistogram,
                                   KioskSessionRestartReason::kStopped, 1);
  }
  histogram()->ExpectTotalCount(kKioskSessionRestartReasonHistogram, 1);
}

TEST_P(AppSessionRestartReasonTest, CrashMetric) {
  const KioskSessionRestartTestCase& test_config = GetParam();
  // Setup `kKioskSessionStartTime` and add a file to the crash directory to
  // emulate previous kiosk session crash.
  base::Value::Dict value;
  value.Set(kKioskSessionStartTime,
            base::TimeToValue(base::Time::Now() - base::Hours(1)));
  local_state()->SetDict(prefs::kKioskMetrics, std::move(value));
  base::FilePath crash_file;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(crash_path(), &crash_file));
  if (test_config.run_with_reboot) {
    EmulateDeviceReboot();
  }

  StartWebKioskSession();

  CheckSessionRestartReasonHistogramDependingOnRebootStatus(
      test_config.run_with_reboot, KioskSessionRestartReason::kCrashed,
      KioskSessionRestartReason::kCrashedWithReboot, histogram());
}

TEST_P(AppSessionRestartReasonTest, LocalStateWasNotSavedMetric) {
  const KioskSessionRestartTestCase& test_config = GetParam();
  // Setup `kKioskSessionStartTime` to emulate previous kiosk session stopped
  // correctly, but because of race condition, `kKioskSessionStartTime` was not
  // cleaned.
  base::Value::Dict value;
  value.Set(kKioskSessionStartTime,
            base::TimeToValue(base::Time::Now() - base::Hours(1)));
  local_state()->SetDict(prefs::kKioskMetrics, std::move(value));
  if (test_config.run_with_reboot) {
    EmulateDeviceReboot();
  }

  StartWebKioskSession();

  CheckSessionRestartReasonHistogramDependingOnRebootStatus(
      test_config.run_with_reboot,
      KioskSessionRestartReason::kLocalStateWasNotSaved,
      KioskSessionRestartReason::kLocalStateWasNotSavedWithReboot, histogram());
}

#if BUILDFLAG(ENABLE_PLUGINS)
TEST_P(AppSessionRestartReasonTest, PluginCrashedMetric) {
  const KioskSessionRestartTestCase& test_config = GetParam();
  StartWebKioskSession();

  KioskSessionPluginHandlerDelegate* delegate = GetPluginHandlerDelegate();
  delegate->OnPluginCrashed(base::FilePath(kBrowserPluginFilePath));

  // Emulate exiting the kiosk session.
  CloseMainBrowser();
  EXPECT_TRUE(IsSessionShuttingDown());
  if (test_config.run_with_reboot) {
    EmulateDeviceReboot();
  }
  histogram()->ExpectTotalCount(kKioskSessionRestartReasonHistogram, 0);

  StartWebKioskSession();

  CheckSessionRestartReasonHistogramDependingOnRebootStatus(
      test_config.run_with_reboot, KioskSessionRestartReason::kPluginCrashed,
      KioskSessionRestartReason::kPluginCrashedWithReboot, histogram());
}

TEST_P(AppSessionRestartReasonTest, PluginHungMetric) {
  const KioskSessionRestartTestCase& test_config = GetParam();
  // Create a fake power manager client.
  // FakePowerManagerClient client;
  StartWebKioskSession();

  KioskSessionPluginHandlerDelegate* delegate = GetPluginHandlerDelegate();
  delegate->OnPluginHung(std::set<int>());

  // Emulate exiting the kiosk session.
  CloseMainBrowser();
  EXPECT_TRUE(IsSessionShuttingDown());
  if (test_config.run_with_reboot) {
    EmulateDeviceReboot();
  }
  histogram()->ExpectTotalCount(kKioskSessionRestartReasonHistogram, 0);

  StartWebKioskSession();

  CheckSessionRestartReasonHistogramDependingOnRebootStatus(
      test_config.run_with_reboot, KioskSessionRestartReason::kPluginHung,
      KioskSessionRestartReason::kPluginHungWithReboot, histogram());
}
#endif  // BUILDFLAG(ENABLE_PLUGINS)

INSTANTIATE_TEST_SUITE_P(
    AppSessionRestartReasons,
    AppSessionRestartReasonTest,
    testing::ValuesIn<KioskSessionRestartTestCase>({
        {/*test_name=*/"WithReboot", /*run_with_reboot=*/false},
        {/*test_name=*/"WithoutReboot", /*run_with_reboot=*/true},
    }),
    [](const testing::TestParamInfo<AppSessionRestartReasonTest::ParamType>&
           info) { return info.param.test_name; });

TEST_F(AppSessionRestartReasonTest, PowerManagerRequestRestart) {
  std::vector<KioskSessionPowerManagerRequestRestartTestCase> test_cases = {
      {/*power_manager_reason=*/power_manager::RequestRestartReason::
           REQUEST_RESTART_SCHEDULED_REBOOT_POLICY,
       /*restart_reason=*/KioskSessionRestartReason::kRebootPolicy},
      {/*power_manager_reason=*/power_manager::RequestRestartReason::
           REQUEST_RESTART_REMOTE_ACTION_REBOOT,
       /*restart_reason=*/KioskSessionRestartReason::kRemoteActionReboot},
      {/*power_manager_reason=*/power_manager::RequestRestartReason::
           REQUEST_RESTART_API,
       /*restart_reason=*/KioskSessionRestartReason::kRestartApi}};

  for (auto test_case : test_cases) {
    StartWebKioskSession();
    chromeos::FakePowerManagerClient::Get()->RequestRestart(
        test_case.power_manager_reason, "test reboot description");
    // Emulate exiting the kiosk session.
    CloseMainBrowser();
    EXPECT_TRUE(IsSessionShuttingDown());

    StartWebKioskSession();

    histogram()->ExpectBucketCount(kKioskSessionRestartReasonHistogram,
                                   test_case.restart_reason, 1);
  }
}

// Kiosk type agnostic test class. Runs all tests for web and chrome app kiosks.
class AppSessionTroubleshootingTest : public AppSessionBaseTest<bool> {
 public:
  void SetUpKioskSession() {
    bool is_web_kiosk = GetParam();
    if (is_web_kiosk) {
      StartWebKioskSession();
      return;
    }
    StartChromeAppKioskSession();
  }

  void UpdateTroubleshootingToolsPolicy(bool enable) {
    GetPrefs()->SetBoolean(prefs::kKioskTroubleshootingToolsEnabled, enable);
  }

  std::unique_ptr<Browser> CreateDevToolsBrowserWithTestWindow() {
    auto params = Browser::CreateParams::CreateForDevTools(profile());

    TestBrowserWindow* test_window_ = new TestBrowserWindow;
    new TestBrowserWindowOwner(test_window_);
    params.window = test_window_;

    return base::WrapUnique<Browser>(Browser::Create(params));
  }

  std::unique_ptr<Browser> CreateRegularBrowserWithTestWindow() {
    return CreateBrowserWithTestWindowAndType(Browser::TYPE_NORMAL);
  }

  std::unique_ptr<Browser> CreateBrowserWithTestWindowAndType(
      Browser::Type type) {
    Browser::CreateParams params(profile(), /*user_gesture=*/true);
    params.type = type;
    return CreateBrowserWithTestWindowForParams(params);
  }
};

TEST_P(AppSessionTroubleshootingTest, EnableTroubleshootingToolsDuringSession) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  // Kiosk session should shoutdown only if policy is changed from enable to
  // disable.
  EXPECT_FALSE(IsSessionShuttingDown());

  UpdateTroubleshootingToolsPolicy(/*enable=*/false);
  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_P(AppSessionTroubleshootingTest,
       EnableTroubleshootingToolsBeforeSessionStarted) {
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);
  SetUpKioskSession();

  UpdateTroubleshootingToolsPolicy(/*enable=*/false);
  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_P(AppSessionTroubleshootingTest,
       MainBrowserShutdownAfterKioskTroubleshootingToolsDisabled) {
  GetPrefs()->SetBoolean(prefs::kKioskTroubleshootingToolsEnabled, true);

  SetUpKioskSession();

  GetPrefs()->SetBoolean(prefs::kKioskTroubleshootingToolsEnabled, false);

  EXPECT_TRUE(IsSessionShuttingDown());

  CloseMainBrowser();

  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_P(AppSessionTroubleshootingTest, OpenDevToolsEnabledTroubleshootingTools) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  EXPECT_FALSE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateDevToolsBrowserWithTestWindow()->window()));

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kOpenedDevToolsBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);
}

TEST_P(AppSessionTroubleshootingTest, CloseTroubleshootingToolsByDefault) {
  SetUpKioskSession();

  // Kiosk troubleshooting tools are disabled by default.
  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateDevToolsBrowserWithTestWindow()->window()));
  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateRegularBrowserWithTestWindow()->window()));

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 2);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 2);
}

TEST_P(AppSessionTroubleshootingTest,
       OpenDevToolsDisableTroubleshootingToolsDuringSession) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  // Kiosk session should shoutdown only if policy is changed from enable to
  // disable.
  EXPECT_FALSE(IsSessionShuttingDown());
  EXPECT_FALSE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateDevToolsBrowserWithTestWindow()->window()));

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kOpenedDevToolsBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);

  UpdateTroubleshootingToolsPolicy(/*enable=*/false);
  EXPECT_TRUE(IsSessionShuttingDown());
}

TEST_P(AppSessionTroubleshootingTest,
       OpenNewWindowEnabledTroubleshootingTools) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  std::unique_ptr<Browser> normal_browser =
      CreateRegularBrowserWithTestWindow();
  EXPECT_FALSE(
      ShouldBrowserBeClosedByAppSessionBrowserHander(normal_browser->window()));

  histogram()->ExpectBucketCount(
      kKioskNewBrowserWindowHistogram,
      KioskBrowserWindowType::kOpenedTroubleshootingNormalBrowser, 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);
}

TEST_P(AppSessionTroubleshootingTest,
       CloseNewWindowDisabledTroubleshootingTools) {
  UpdateTroubleshootingToolsPolicy(/*enable=*/false);
  SetUpKioskSession();

  EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
      CreateRegularBrowserWithTestWindow()->window()));

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 1);
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram, 1);
}

TEST_P(AppSessionTroubleshootingTest,
       OnlyAllowRegularBrowserAndDevToolsAsTroubleshootingBrowsers) {
  const std::vector<Browser::Type> should_be_closed_browser_types = {
    Browser::Type::TYPE_POPUP,
    Browser::Type::TYPE_APP,
    Browser::Type::TYPE_APP_POPUP,
#if BUILDFLAG(IS_CHROMEOS_ASH)
    Browser::Type::TYPE_CUSTOM_TAB,
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
    Browser::Type::TYPE_PICTURE_IN_PICTURE,
  };
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  for (Browser::Type type : should_be_closed_browser_types) {
    EXPECT_TRUE(ShouldBrowserBeClosedByAppSessionBrowserHander(
        CreateBrowserWithTestWindowAndType(type)->window()));
  }

  histogram()->ExpectBucketCount(kKioskNewBrowserWindowHistogram,
                                 KioskBrowserWindowType::kClosedRegularBrowser,
                                 should_be_closed_browser_types.size());
  histogram()->ExpectTotalCount(kKioskNewBrowserWindowHistogram,
                                should_be_closed_browser_types.size());
}

INSTANTIATE_TEST_SUITE_P(AppSessionTroubleshootingTools,
                         AppSessionTroubleshootingTest,
                         ::testing::Bool());

#if BUILDFLAG(IS_CHROMEOS_ASH)
class FakeNewWindowDelegate : public ash::TestNewWindowDelegate {
 public:
  FakeNewWindowDelegate() = default;
  ~FakeNewWindowDelegate() override = default;

  void NewWindow(bool incognito, bool should_trigger_session_restore) override {
    new_window_called_ = true;
  }

  void NewTab() override { new_tab_called_ = true; }

  void ShowTaskManager() override { task_manager_called_ = true; }

  void OpenFeedbackPage(FeedbackSource source,
                        const std::string& description_template) override {
    open_feedback_page_called_ = true;
  }

  bool is_new_window_called() const { return new_window_called_; }
  bool is_new_tab_called() const { return new_tab_called_; }
  bool is_task_manager_called() const { return task_manager_called_; }
  bool is_open_feedback_page_called() const {
    return open_feedback_page_called_;
  }

 private:
  bool new_window_called_ = false;
  bool new_tab_called_ = false;
  bool task_manager_called_ = false;
  bool open_feedback_page_called_ = false;
};

// Tests actions after pressing troubleshooting shortcuts. Runs all tests for
// web and chrome app kiosks.
class AppSessionTroubleshootingShortcutsTest
    : public AppSessionTroubleshootingTest {
 public:
  static bool ProcessInController(const ui::Accelerator& accelerator) {
    return ash::Shell::Get()->accelerator_controller()->Process(accelerator);
  }

  void SetUp() override {
    auto new_window_delegate = std::make_unique<FakeNewWindowDelegate>();
    fake_new_window_delegate_ = new_window_delegate.get();
    test_new_window_delegate_provider_ =
        std::make_unique<ash::TestNewWindowDelegateProvider>(
            std::move(new_window_delegate));

    AppSessionTroubleshootingTest::SetUp();

    ash::SessionInfo info;
    info.is_running_in_app_mode = true;
    info.state = session_manager::SessionState::ACTIVE;
    ash::Shell::Get()->session_controller()->SetSessionInfo(info);
  }

  bool IsOverviewToggled() const {
    ash::OverviewController* overview_controller =
        ash::Shell::Get()->overview_controller();
    return overview_controller->InOverviewSession();
  }

  bool IsNewWindowCalled() const {
    return fake_new_window_delegate_->is_new_window_called();
  }

  bool IsNewTabCalled() const {
    return fake_new_window_delegate_->is_new_tab_called();
  }

  bool IsTaskManagerCalled() const {
    return fake_new_window_delegate_->is_task_manager_called();
  }

  bool IsOpenFeedbackPageCalled() const {
    return fake_new_window_delegate_->is_open_feedback_page_called();
  }

 protected:
  ui::Accelerator new_window_accelerator =
      ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN);
  ui::Accelerator task_manager_accelerator =
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_COMMAND_DOWN);
  ui::Accelerator open_feedback_page_accelerator =
      ui::Accelerator(ui::VKEY_I, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN);
  ui::Accelerator toggle_overview_accelerator =
      ui::Accelerator(ui::VKEY_MEDIA_LAUNCH_APP1, ui::EF_NONE);

 private:
  raw_ptr<FakeNewWindowDelegate, DanglingUntriaged> fake_new_window_delegate_;
  std::unique_ptr<ash::TestNewWindowDelegateProvider>
      test_new_window_delegate_provider_;
};

TEST_P(AppSessionTroubleshootingShortcutsTest, NewWindowShortcutEnabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  ProcessInController(new_window_accelerator);
  EXPECT_TRUE(IsNewWindowCalled());
}

// Just confirm that other shortcuts (e.g. new tab) do not work.
TEST_P(AppSessionTroubleshootingShortcutsTest, NewTabShortcutIsNoAction) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  ProcessInController(ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN));
  EXPECT_FALSE(IsNewTabCalled());
  EXPECT_FALSE(IsNewWindowCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       NewWindowShortcutNoActionByDefault) {
  SetUpKioskSession();

  ProcessInController(new_window_accelerator);
  EXPECT_FALSE(IsNewWindowCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       NewWindowShortcutNoActionIfPolicyDisabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/false);

  ProcessInController(new_window_accelerator);
  EXPECT_FALSE(IsNewWindowCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest, TaskManagerShortcutEnabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  ProcessInController(task_manager_accelerator);
  EXPECT_TRUE(IsTaskManagerCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       TaskManagerShortcutNoActionByDefault) {
  SetUpKioskSession();

  ProcessInController(task_manager_accelerator);
  EXPECT_FALSE(IsTaskManagerCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       TaskManagerShortcutNoActionIfPolicyDisabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/false);

  ProcessInController(task_manager_accelerator);
  EXPECT_FALSE(IsTaskManagerCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       OpenFeedbackPageShortcutEnabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  ProcessInController(open_feedback_page_accelerator);
  EXPECT_TRUE(IsOpenFeedbackPageCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       OpenFeedbackPageShortcutNoActionByDefault) {
  SetUpKioskSession();

  ProcessInController(open_feedback_page_accelerator);
  EXPECT_FALSE(IsOpenFeedbackPageCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       OpenFeedbackPageShortcutNoActionIfPolicyDisabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/false);

  ProcessInController(open_feedback_page_accelerator);
  EXPECT_FALSE(IsOpenFeedbackPageCalled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest, ToggleOverviewShortcutEnabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/true);

  ProcessInController(toggle_overview_accelerator);
  EXPECT_TRUE(IsOverviewToggled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       ToggleOverviewShortcutNoActionByDefault) {
  SetUpKioskSession();

  ProcessInController(toggle_overview_accelerator);
  EXPECT_FALSE(IsOverviewToggled());
}

TEST_P(AppSessionTroubleshootingShortcutsTest,
       ToggleOverviewShortcutNoActionIfPolicyDisabled) {
  SetUpKioskSession();
  UpdateTroubleshootingToolsPolicy(/*enable=*/false);

  ProcessInController(toggle_overview_accelerator);
  EXPECT_FALSE(IsOverviewToggled());
}

INSTANTIATE_TEST_SUITE_P(AppSessionTroubleshootingShortcuts,
                         AppSessionTroubleshootingShortcutsTest,
                         ::testing::Bool());

#endif  //  BUILDFLAG(IS_CHROMEOS_ASH)

#if BUILDFLAG(ENABLE_PLUGINS)
TEST_F(AppSessionTest, ShouldHandlePlugin) {
  // Create an out-of-process pepper plugin.
  content::WebPluginInfo info1;
  info1.name = kPepperPluginName1;
  info1.path = base::FilePath(kPepperPluginFilePath1);
  info1.type = content::WebPluginInfo::PLUGIN_TYPE_PEPPER_OUT_OF_PROCESS;

  // Create an in-of-process pepper plugin.
  content::WebPluginInfo info2;
  info2.name = kPepperPluginName2;
  info2.path = base::FilePath(kPepperPluginFilePath2);
  info2.type = content::WebPluginInfo::PLUGIN_TYPE_PEPPER_IN_PROCESS;

  // Create an in-of-process browser (non-pepper) plugin.
  content::WebPluginInfo info3;
  info3.name = kBrowserPluginName;
  info3.path = base::FilePath(kBrowserPluginFilePath);
  info3.type = content::WebPluginInfo::PLUGIN_TYPE_BROWSER_PLUGIN;

  // Register two pepper plugins.
  content::PluginService* service = content::PluginService::GetInstance();
  service->RegisterInternalPlugin(info1, true);
  service->RegisterInternalPlugin(info2, true);
  service->RegisterInternalPlugin(info3, true);
  service->Init();
  service->RefreshPlugins();

  // Force plugins to load and wait for completion.
  base::RunLoop run_loop;
  service->GetPlugins(base::BindOnce(
      [](base::OnceClosure callback,
         const std::vector<content::WebPluginInfo>& ignore) {
        std::move(callback).Run();
      },
      run_loop.QuitClosure()));
  run_loop.Run();

  AppSession app_session(profile());
  KioskSessionPluginHandlerDelegate* delegate =
      app_session.GetPluginHandlerDelegateForTesting();

  // The app session should handle two pepper plugins.
  EXPECT_TRUE(
      delegate->ShouldHandlePlugin(base::FilePath(kPepperPluginFilePath1)));
  EXPECT_TRUE(
      delegate->ShouldHandlePlugin(base::FilePath(kPepperPluginFilePath2)));

  // The app session should not handle the browser plugin.
  EXPECT_FALSE(
      delegate->ShouldHandlePlugin(base::FilePath(kBrowserPluginFilePath)));

  // The app session should not handle the unregistered plugin.
  EXPECT_FALSE(delegate->ShouldHandlePlugin(
      base::FilePath(kUnregisteredPluginFilePath)));
}

TEST_F(AppSessionTest, OnPluginCrashed) {
  StartWebKioskSession();
  KioskSessionPluginHandlerDelegate* delegate = GetPluginHandlerDelegate();

  // Verified the number of restart calls.
  EXPECT_EQ(
      chromeos::FakePowerManagerClient::Get()->num_request_restart_calls(), 0);
  delegate->OnPluginCrashed(base::FilePath(kBrowserPluginFilePath));
  EXPECT_EQ(
      chromeos::FakePowerManagerClient::Get()->num_request_restart_calls(), 1);

  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kPluginCrashed, 1);
  EXPECT_EQ(2u, histogram()->GetAllSamples(kKioskSessionStateHistogram).size());
}

TEST_F(AppSessionTest, OnPluginHung) {
  StartWebKioskSession();
  KioskSessionPluginHandlerDelegate* delegate = GetPluginHandlerDelegate();

  // Only verify if this method can be called without error.
  delegate->OnPluginHung(std::set<int>());
  histogram()->ExpectBucketCount(kKioskSessionStateHistogram,
                                 KioskSessionState::kPluginHung, 1);
  EXPECT_EQ(2u, histogram()->GetAllSamples(kKioskSessionStateHistogram).size());
}
#endif  // BUILDFLAG(ENABLE_PLUGINS)

}  // namespace chromeos

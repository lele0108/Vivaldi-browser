// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/video_conference/video_conference_tray_controller.h"

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/constants/ash_switches.h"
#include "ash/public/cpp/test/test_system_tray_client.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/status_area_widget.h"
#include "ash/system/status_area_widget_test_helper.h"
#include "ash/system/toast/anchored_nudge_manager_impl.h"
#include "ash/system/video_conference/fake_video_conference_tray_controller.h"
#include "ash/system/video_conference/video_conference_common.h"
#include "ash/system/video_conference/video_conference_tray.h"
#include "ash/test/ash_test_base.h"
#include "base/command_line.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "chromeos/ash/components/audio/cras_audio_handler.h"
#include "chromeos/crosapi/mojom/video_conference.mojom.h"
#include "components/prefs/pref_service.h"
#include "media/capture/video/chromeos/mojom/cros_camera_service.mojom-shared.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {

namespace {

// The max amount of times the "Speak-on-mute opt-in" nudge can show.
constexpr int kSpeakOnMuteOptInNudgeMaxShownCount = 3;

constexpr char kVideoConferenceTraySpeakOnMuteDetectedNudgeId[] =
    "video_conference_tray_nudge_ids.speak_on_mute_detected";

constexpr char kVideoConferenceTraySpeakOnMuteOptInNudgeId[] =
    "video_conference_tray_nudge_ids.speak_on_mute_opt_in";

constexpr char kVideoConferenceTrayMicrophoneUseWhileHWDisabledNudgeId[] =
    "video_conference_tray_nudge_ids.microphone_use_while_hw_disabled";
constexpr char kVideoConferenceTrayMicrophoneUseWhileSWDisabledNudgeId[] =
    "video_conference_tray_nudge_ids.microphone_use_while_sw_disabled";
constexpr char kVideoConferenceTrayCameraUseWhileHWDisabledNudgeId[] =
    "video_conference_tray_nudge_ids.camera_use_while_hw_disabled";
constexpr char kVideoConferenceTrayCameraUseWhileSWDisabledNudgeId[] =
    "video_conference_tray_nudge_ids.camera_use_while_sw_disabled";

constexpr char kRepeatedShowsHistogramName[] =
    "Ash.VideoConference.NumberOfRepeatedShows";

bool IsNudgeShown(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->IsNudgeShown(id);
}

const std::u16string& GetNudgeText(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->GetNudgeBodyTextForTest(id);
}

views::View* GetNudgeAnchorView(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->GetNudgeAnchorViewForTest(id);
}

views::LabelButton* GetNudgeDismissButton(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->GetNudgeDismissButtonForTest(
      id);
}

views::LabelButton* GetNudgeSecondButton(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->GetNudgeSecondButtonForTest(
      id);
}

AnchoredNudge* GetShownNudge(const std::string& id) {
  return Shell::Get()->anchored_nudge_manager()->GetShownNudgeForTest(id);
}

}  // namespace

class VideoConferenceTrayControllerTest : public AshTestBase {
 public:
  VideoConferenceTrayControllerTest()
      : AshTestBase(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  VideoConferenceTrayControllerTest(const VideoConferenceTrayControllerTest&) =
      delete;
  VideoConferenceTrayControllerTest& operator=(
      const VideoConferenceTrayControllerTest&) = delete;
  ~VideoConferenceTrayControllerTest() override = default;

  // AshTestBase:
  void SetUp() override {
    scoped_feature_list_.InitAndEnableFeature(features::kVideoConference);
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kCameraEffectsSupportedByHardware);

    // Instantiates a fake controller (the real one is created in
    // ChromeBrowserMainExtraPartsAsh::PreProfileInit() which is not called in
    // ash unit tests).
    controller_ = std::make_unique<FakeVideoConferenceTrayController>();

    AshTestBase::SetUp();
  }

  void TearDown() override {
    AshTestBase::TearDown();
    controller_.reset();
  }

  // Returns the VC tray from the primary display. If testing multiple displays,
  // VC nudges will be shown anchored to the tray in the active display.
  VideoConferenceTray* video_conference_tray() {
    return StatusAreaWidgetTestHelper::GetStatusAreaWidget()
        ->video_conference_tray();
  }

  VideoConferenceTrayButton* camera_icon() {
    return video_conference_tray()->camera_icon();
  }

  VideoConferenceTrayButton* audio_icon() {
    return video_conference_tray()->audio_icon();
  }

  // Make the tray and buttons visible by setting `VideoConferenceMediaState`,
  // and return the state so it can be modified.
  VideoConferenceMediaState SetTrayAndButtonsVisible() {
    VideoConferenceMediaState state;
    state.has_media_app = true;
    state.has_camera_permission = true;
    state.has_microphone_permission = true;
    state.is_capturing_screen = true;
    state.is_capturing_microphone = true;
    controller()->UpdateWithMediaState(state);
    return state;
  }

  void ToggleVcTrayBubble() {
    LeftClickOn(video_conference_tray()->toggle_bubble_button_);
  }

  FakeVideoConferenceTrayController* controller() { return controller_.get(); }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  std::unique_ptr<FakeVideoConferenceTrayController> controller_;
};

TEST_F(VideoConferenceTrayControllerTest, UpdateButtonWhenCameraMuted) {
  EXPECT_FALSE(camera_icon()->toggled());
  EXPECT_FALSE(camera_icon()->show_privacy_indicator());

  VideoConferenceMediaState state;
  state.is_capturing_camera = true;
  controller()->UpdateWithMediaState(state);
  EXPECT_TRUE(camera_icon()->show_privacy_indicator());

  // When camera is detected to be muted, the icon should be toggled and doesn't
  // show the privacy indicator.
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::ON);
  EXPECT_TRUE(camera_icon()->toggled());
  EXPECT_FALSE(camera_icon()->show_privacy_indicator());

  // When unmuted, privacy indicator should show back.
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::OFF);
  EXPECT_FALSE(camera_icon()->toggled());
  EXPECT_TRUE(camera_icon()->show_privacy_indicator());
}

TEST_F(VideoConferenceTrayControllerTest, UpdateButtonWhenMicrophoneMuted) {
  EXPECT_FALSE(audio_icon()->toggled());
  EXPECT_FALSE(audio_icon()->show_privacy_indicator());

  VideoConferenceMediaState state;
  state.is_capturing_microphone = true;
  controller()->UpdateWithMediaState(state);
  EXPECT_TRUE(audio_icon()->show_privacy_indicator());

  // When microphone is detected to be muted, the icon should be toggled and
  // doesn't show the privacy indicator.
  controller()->OnInputMuteChanged(
      /*mute_on=*/true, CrasAudioHandler::InputMuteChangeMethod::kOther);
  EXPECT_TRUE(audio_icon()->toggled());
  EXPECT_FALSE(audio_icon()->show_privacy_indicator());

  // When unmuted, privacy indicator should show back.
  controller()->OnInputMuteChanged(
      /*mute_on=*/false, CrasAudioHandler::InputMuteChangeMethod::kOther);
  EXPECT_FALSE(audio_icon()->toggled());
  EXPECT_TRUE(audio_icon()->show_privacy_indicator());
}

TEST_F(VideoConferenceTrayControllerTest, CameraHardwareMuted) {
  // The camera icon should only be un-toggled if it is not hardware and
  // software muted.
  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::ON);
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::ON);
  EXPECT_TRUE(camera_icon()->toggled());

  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::ON);
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::OFF);
  EXPECT_TRUE(camera_icon()->toggled());

  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::OFF);
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::ON);
  EXPECT_TRUE(camera_icon()->toggled());

  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::OFF);
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::OFF);
  EXPECT_FALSE(camera_icon()->toggled());
}

TEST_F(VideoConferenceTrayControllerTest, ClickCameraWhenHardwareMuted) {
  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::ON);
  EXPECT_TRUE(camera_icon()->toggled());

  // Clicking the camera button when it is hardware-muted should not un-toggle
  // the button.
  LeftClickOn(camera_icon());
  EXPECT_TRUE(camera_icon()->toggled());
}

TEST_F(VideoConferenceTrayControllerTest,
       HandleCameraUsedWhileSoftwaredDisabled) {
  auto* app_name = u"app_name";
  auto camera_device_name =
      l10n_util::GetStringUTF16(IDS_ASH_VIDEO_CONFERENCE_CAMERA_NAME);
  auto* nudge_id = kVideoConferenceTrayCameraUseWhileSWDisabledNudgeId;

  SetTrayAndButtonsVisible();

  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::ON);

  // No nudge is shown before `HandleDeviceUsedWhileDisabled()` is called.
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  controller()->HandleDeviceUsedWhileDisabled(
      crosapi::mojom::VideoConferenceMediaDevice::kCamera, app_name);

  // Nudge should be displayed. Showing that app is accessing while camera is
  // software-muted.
  ASSERT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(GetNudgeAnchorView(nudge_id), camera_icon());
  EXPECT_EQ(GetNudgeText(nudge_id),
            l10n_util::GetStringFUTF16(
                IDS_ASH_VIDEO_CONFERENCE_TOAST_USE_WHILE_SOFTWARE_DISABLED,
                app_name, camera_device_name));

  // Unmute camera through SW. Nudge should be dismissed.
  controller()->OnCameraSWPrivacySwitchStateChanged(
      cros::mojom::CameraPrivacySwitchState::OFF);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest,
       HandleMicrophoneUsedWhileSoftwaredDisabled) {
  auto* app_name = u"app_name";
  auto microphone_device_name =
      l10n_util::GetStringUTF16(IDS_ASH_VIDEO_CONFERENCE_MICROPHONE_NAME);
  auto* nudge_id = kVideoConferenceTrayMicrophoneUseWhileSWDisabledNudgeId;

  SetTrayAndButtonsVisible();

  controller()->OnInputMuteChanged(
      /*mute_on=*/true, CrasAudioHandler::InputMuteChangeMethod::kOther);

  // No nudge is shown before `HandleDeviceUsedWhileDisabled()` is called.
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  controller()->HandleDeviceUsedWhileDisabled(
      crosapi::mojom::VideoConferenceMediaDevice::kMicrophone, app_name);

  // Nudge should be displayed. Showing that app is accessing while microphone
  // is software-muted.
  ASSERT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(GetNudgeAnchorView(nudge_id), audio_icon());
  EXPECT_EQ(GetNudgeText(nudge_id),
            l10n_util::GetStringFUTF16(
                IDS_ASH_VIDEO_CONFERENCE_TOAST_USE_WHILE_SOFTWARE_DISABLED,
                app_name, microphone_device_name));

  // Unmute microphone through SW. Nudge should be dismissed.
  controller()->OnInputMuteChanged(
      /*mute_on=*/false, CrasAudioHandler::InputMuteChangeMethod::kOther);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest,
       HandleCameraUsedWhileHardwaredDisabled) {
  auto* app_name = u"app_name";
  auto camera_device_name =
      l10n_util::GetStringUTF16(IDS_ASH_VIDEO_CONFERENCE_CAMERA_NAME);
  auto* nudge_id = kVideoConferenceTrayCameraUseWhileHWDisabledNudgeId;

  SetTrayAndButtonsVisible();

  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::ON);

  // No nudge is shown before `HandleDeviceUsedWhileDisabled()` is called.
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  controller()->HandleDeviceUsedWhileDisabled(
      crosapi::mojom::VideoConferenceMediaDevice::kCamera, app_name);

  // Nudge should be displayed. Showing that app is accessing while camera is
  // hardware-muted.
  ASSERT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(GetNudgeAnchorView(nudge_id), camera_icon());
  EXPECT_EQ(GetNudgeText(nudge_id),
            l10n_util::GetStringFUTF16(
                IDS_ASH_VIDEO_CONFERENCE_TOAST_USE_WHILE_HARDWARE_DISABLED,
                app_name, camera_device_name));

  // Unmute camera through HW. Nudge should be dismissed.
  controller()->OnCameraHWPrivacySwitchStateChanged(
      /*device_id=*/"device_id", cros::mojom::CameraPrivacySwitchState::OFF);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest,
       HandleMicrophoneUsedWhileHardwaredDisabled) {
  auto* app_name = u"app_name";
  auto microphone_device_name =
      l10n_util::GetStringUTF16(IDS_ASH_VIDEO_CONFERENCE_MICROPHONE_NAME);
  auto* nudge_id = kVideoConferenceTrayMicrophoneUseWhileHWDisabledNudgeId;

  SetTrayAndButtonsVisible();

  controller()->OnInputMuteChanged(
      /*mute_on=*/true,
      CrasAudioHandler::InputMuteChangeMethod::kPhysicalShutter);

  // No nudge is shown before `HandleDeviceUsedWhileDisabled()` is called.
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  controller()->HandleDeviceUsedWhileDisabled(
      crosapi::mojom::VideoConferenceMediaDevice::kMicrophone, app_name);

  // Nudge should be displayed. Showing that app is accessing while microphone
  // is hardware-muted.
  ASSERT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(GetNudgeAnchorView(nudge_id), audio_icon());
  EXPECT_EQ(GetNudgeText(nudge_id),
            l10n_util::GetStringFUTF16(
                IDS_ASH_VIDEO_CONFERENCE_TOAST_USE_WHILE_HARDWARE_DISABLED,
                app_name, microphone_device_name));

  // Unmute microphone through HW. Nudge should be dismissed.
  controller()->OnInputMuteChanged(
      /*mute_on=*/false,
      CrasAudioHandler::InputMuteChangeMethod::kPhysicalShutter);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteNudge) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteDetectedNudgeId;

  SetTrayAndButtonsVisible();

  // No nudge is shown before `OnSpeakOnMuteDetected()` is called.
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Nudge should be displayed. Showing that client is speaking while on mute.
  controller()->OnSpeakOnMuteDetected();
  ASSERT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(GetNudgeAnchorView(nudge_id), audio_icon());
  EXPECT_EQ(GetNudgeText(nudge_id),
            l10n_util::GetStringUTF16(
                IDS_ASH_VIDEO_CONFERENCE_TOAST_SPEAK_ON_MUTE_DETECTED));

  AnchoredNudgeManager::Get()->Cancel(nudge_id);

  // Nudge should not be displayed as there is a cool down period for the nudge.
  controller()->OnSpeakOnMuteDetected();
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  controller()->OnInputMuteChanged(
      /*mute_on=*/false,
      CrasAudioHandler::InputMuteChangeMethod::kPhysicalShutter);
  controller()->OnInputMuteChanged(
      /*mute_on=*/true,
      CrasAudioHandler::InputMuteChangeMethod::kPhysicalShutter);

  // Nudge should be displayed again as the mute action will reset the nudge
  // cool down timer.
  controller()->OnSpeakOnMuteDetected();
  EXPECT_TRUE(IsNudgeShown(nudge_id));

  // Unmute microphone through HW. Nudge should be dismissed.
  controller()->OnInputMuteChanged(
      /*mute_on=*/false,
      CrasAudioHandler::InputMuteChangeMethod::kPhysicalShutter);
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Mute microphone through SW and show nudge again.
  controller()->OnInputMuteChanged(
      /*mute_on=*/true, CrasAudioHandler::InputMuteChangeMethod::kOther);
  controller()->OnSpeakOnMuteDetected();
  EXPECT_TRUE(IsNudgeShown(nudge_id));

  // Unmute microphone through SW. Nudge should be dismissed.
  controller()->OnInputMuteChanged(
      /*mute_on=*/false, CrasAudioHandler::InputMuteChangeMethod::kOther);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteNudgeClick) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteDetectedNudgeId;

  SetTrayAndButtonsVisible();

  // Nudge should be displayed. Showing that client is speaking while on mute.
  controller()->OnSpeakOnMuteDetected();
  ASSERT_TRUE(IsNudgeShown(nudge_id));

  // Clicks on the nudge should open the settings page.
  EXPECT_EQ(GetSystemTrayClient()->show_speak_on_mute_detection_count(), 0);
  LeftClickOn(GetShownNudge(nudge_id));
  EXPECT_EQ(GetSystemTrayClient()->show_speak_on_mute_detection_count(), 1);
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteNudgeTimeFrame) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteDetectedNudgeId;

  SetTrayAndButtonsVisible();

  // Nudge should be displayed. Showing that client is speaking while on mute,
  // and the 60 seconds cool down counter starts at the same time.
  controller()->OnSpeakOnMuteDetected();
  ASSERT_TRUE(IsNudgeShown(nudge_id));

  // Wait for 20 seconds to simulate that the nudge has disappeared.
  task_environment()->AdvanceClock(base::Seconds(20));
  AnchoredNudgeManager::Get()->Cancel(nudge_id);

  // Nudge should not be displayed at 20 seconds as there is a cool down period
  // for the nudge.
  controller()->OnSpeakOnMuteDetected();
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Wait for another 20 seconds and the cool down has not passed yet.
  task_environment()->AdvanceClock(base::Seconds(20));

  // Nudge should not be displayed at 40 seconds as there is a cool down period
  // for the nudge.
  controller()->OnSpeakOnMuteDetected();
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Wait for another 20 seconds to so that the 60 seconds cool down periods has
  // passed.
  task_environment()->AdvanceClock(base::Seconds(60));

  // Nudge should be displayed as the cool down period has passed.
  controller()->OnSpeakOnMuteDetected();
  ASSERT_TRUE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest, RecordRepeatedShows) {
  // Set up 2 displays. Note that only one instance should be recorded for the
  // primary display when there are repeated shows.
  UpdateDisplay("100x200,300x400");

  base::HistogramTester histograms;

  auto flicker_vc_tray = [](int number_of_flicker,
                            FakeVideoConferenceTrayController* controller,
                            base::test::TaskEnvironment* task_environment) {
    // Makes the view flicker (show then hide) for `number_of_flicker` of times.
    for (auto i = 0; i < number_of_flicker; i++) {
      VideoConferenceMediaState state;
      state.has_media_app = true;
      controller->UpdateWithMediaState(state);

      state.has_media_app = false;
      controller->UpdateWithMediaState(state);

      task_environment->FastForwardBy(base::Milliseconds(80));
    }
    task_environment->FastForwardBy(base::Milliseconds(100));
  };

  int expected_sample = 6;
  flicker_vc_tray(expected_sample, controller(), task_environment());
  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, expected_sample, 1);

  // Makes one more flickering after 100ms. This flicker should not count
  // towards the previous ones, but this will be counted in a bucket for 1 show.
  VideoConferenceMediaState state;
  state.has_media_app = true;
  controller()->UpdateWithMediaState(state);

  state.has_media_app = false;
  controller()->UpdateWithMediaState(state);
  task_environment()->FastForwardBy(base::Milliseconds(100));

  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, expected_sample + 1,
                               0);
  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, 1, 1);

  // Make sure it works again.
  flicker_vc_tray(8, controller(), task_environment());
  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, 8, 1);

  flicker_vc_tray(2, controller(), task_environment());
  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, 2, 1);

  flicker_vc_tray(1, controller(), task_environment());
  histograms.ExpectBucketCount(kRepeatedShowsHistogramName, 1, 2);
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteOptInNudge) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteOptInNudgeId;

  // Ensure relevant prefs have been registered.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  EXPECT_TRUE(prefs->FindPreference(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_TRUE(prefs->FindPreference(prefs::kSpeakOnMuteOptInNudgeShownCount));

  SetTrayAndButtonsVisible();
  EXPECT_TRUE(video_conference_tray()->GetVisible());

  // Nudge has not been shown more than its max number of times.
  EXPECT_EQ(0, prefs->GetInteger(prefs::kSpeakOnMuteOptInNudgeShownCount));
  EXPECT_TRUE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));

  // Microphone is not muted, nudge is not shown.
  EXPECT_FALSE(controller()->GetMicrophoneMuted());
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Microphone was just muted, nudge is shown.
  controller()->SetMicrophoneMuted(true);
  EXPECT_TRUE(controller()->GetMicrophoneMuted());
  EXPECT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(1, prefs->GetInteger(prefs::kSpeakOnMuteOptInNudgeShownCount));

  // Microphone was unmuted, nudge is cancelled.
  controller()->SetMicrophoneMuted(false);
  EXPECT_FALSE(controller()->GetMicrophoneMuted());
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Microphone was just muted again, nudge is shown.
  controller()->SetMicrophoneMuted(true);
  EXPECT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(2, prefs->GetInteger(prefs::kSpeakOnMuteOptInNudgeShownCount));

  // Open VC tray bubble, nudge is cancelled.
  ToggleVcTrayBubble();
  EXPECT_FALSE(IsNudgeShown(nudge_id));

  // Close bubble, unmute and mute again, nudge is shown.
  ToggleVcTrayBubble();
  controller()->SetMicrophoneMuted(false);
  controller()->SetMicrophoneMuted(true);
  EXPECT_TRUE(IsNudgeShown(nudge_id));
  EXPECT_EQ(3, prefs->GetInteger(prefs::kSpeakOnMuteOptInNudgeShownCount));

  // Nudge has been shown its max number of times, it should not show again.
  EXPECT_EQ(kSpeakOnMuteOptInNudgeMaxShownCount,
            prefs->GetInteger(prefs::kSpeakOnMuteOptInNudgeShownCount));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));

  // Unmute and mute again, nudge has reached its max shown count, so it won't
  // be shown again.
  controller()->SetMicrophoneMuted(false);
  controller()->SetMicrophoneMuted(true);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteOptInNudge_OptOut) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteOptInNudgeId;

  // Ensure relevant prefs have been registered.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  EXPECT_TRUE(prefs->FindPreference(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_TRUE(prefs->FindPreference(prefs::kUserSpeakOnMuteDetectionEnabled));

  SetTrayAndButtonsVisible();
  EXPECT_TRUE(video_conference_tray()->GetVisible());

  // Nudge has not been shown or interacted with. The speak-on-mute feature has
  // not been enabled through the nudge or through settings.
  EXPECT_TRUE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kUserSpeakOnMuteDetectionEnabled));

  // Microphone was just muted, nudge is shown.
  controller()->SetMicrophoneMuted(true);
  EXPECT_TRUE(IsNudgeShown(nudge_id));

  // Opt out of speak-on-mute. Nudge should be dismissed and never shown again.
  LeftClickOn(GetNudgeDismissButton(nudge_id));
  EXPECT_FALSE(IsNudgeShown(nudge_id));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kUserSpeakOnMuteDetectionEnabled));

  // Unmute and mute again, user opted out so nudge should not be shown.
  controller()->SetMicrophoneMuted(false);
  controller()->SetMicrophoneMuted(true);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

TEST_F(VideoConferenceTrayControllerTest, SpeakOnMuteOptInNudge_OptIn) {
  auto* nudge_id = kVideoConferenceTraySpeakOnMuteOptInNudgeId;

  // Ensure relevant prefs have been registered.
  PrefService* prefs =
      Shell::Get()->session_controller()->GetLastActiveUserPrefService();
  EXPECT_TRUE(prefs->FindPreference(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_TRUE(prefs->FindPreference(prefs::kUserSpeakOnMuteDetectionEnabled));

  SetTrayAndButtonsVisible();
  EXPECT_TRUE(video_conference_tray()->GetVisible());

  // Nudge has not been shown or interacted with. The speak-on-mute feature has
  // not been enabled through the nudge or through settings.
  EXPECT_TRUE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kUserSpeakOnMuteDetectionEnabled));

  // Microphone was just muted, nudge is shown.
  controller()->SetMicrophoneMuted(true);
  EXPECT_TRUE(IsNudgeShown(nudge_id));

  // Opt in to speak-on-mute. Nudge should be dismissed and never shown again.
  LeftClickOn(GetNudgeSecondButton(nudge_id));
  EXPECT_FALSE(IsNudgeShown(nudge_id));
  EXPECT_FALSE(prefs->GetBoolean(prefs::kShouldShowSpeakOnMuteOptInNudge));
  EXPECT_TRUE(prefs->GetBoolean(prefs::kUserSpeakOnMuteDetectionEnabled));

  // Unmute and mute again, user opted in so nudge should not be shown.
  controller()->SetMicrophoneMuted(false);
  controller()->SetMicrophoneMuted(true);
  EXPECT_FALSE(IsNudgeShown(nudge_id));
}

}  // namespace ash

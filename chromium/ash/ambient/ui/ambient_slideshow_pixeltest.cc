// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/ambient/ui/media_string_view.h"

#include <array>
#include <tuple>

#include "ash/ambient/test/ambient_ash_test_base.h"
#include "ash/public/cpp/ambient/ambient_ui_model.h"
#include "ash/public/cpp/style/dark_light_mode_controller.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/test/pixel/ash_pixel_differ.h"
#include "base/strings/strcat.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/display.h"

namespace ash {

namespace {

using TestParams = std::tuple<absl::optional<media_session::MediaMetadata>,
                              /*dark_mode=*/bool,
                              /*rtl=*/bool>;

std::array<absl::optional<media_session::MediaMetadata>, 3>
GetMediaMedataVariations() {
  media_session::MediaMetadata short_metadata;
  short_metadata.artist = u"artist";
  short_metadata.title = u"title";

  media_session::MediaMetadata long_metadata;
  long_metadata.artist = u"A super duper long artist name";
  long_metadata.title = u"A super duper long title";

  return {std::move(short_metadata), std::move(long_metadata), absl::nullopt};
}

bool IsDarkMode(const TestParams& param) {
  return std::get<1>(param);
}

bool IsRtl(const TestParams& param) {
  return std::get<2>(param);
}

const absl::optional<media_session::MediaMetadata>& GetMediaMetadata(
    const TestParams& param) {
  return std::get<absl::optional<media_session::MediaMetadata>>(param);
}

std::string GetName(const testing::TestParamInfo<TestParams>& param_info) {
  const absl::optional<media_session::MediaMetadata>& metadata =
      GetMediaMetadata(param_info.param);
  std::string metadata_description_text;
  if (!metadata.has_value()) {
    metadata_description_text = "Nomedia";
  } else if (metadata->artist == u"artist") {
    metadata_description_text = "Shortmedia";
  } else {
    metadata_description_text = "Longmedia";
  }
  return base::StrCat({metadata_description_text,
                       IsDarkMode(param_info.param) ? "Dark" : "Light",
                       IsRtl(param_info.param) ? "Rtl" : "Ltr"});
}

}  // namespace

class AmbientSlideshowPixelTest
    : public AmbientAshTestBase,
      public testing::WithParamInterface<TestParams> {
 public:
  absl::optional<pixel_test::InitParams> CreatePixelTestInitParams()
      const override {
    pixel_test::InitParams pixel_test_init_params;
    pixel_test_init_params.under_rtl = IsRtl(GetParam());
    return pixel_test_init_params;
  }

  void SetUp() override {
    AmbientAshTestBase::SetUp();
    SetAmbientTheme(AmbientTheme::kSlideshow);
    GetSessionControllerClient()->set_show_lock_screen_views(true);
    DarkLightModeController::Get()->SetDarkModeEnabledForTest(
        IsDarkMode(GetParam()));
    SetDecodedPhotoColor(SK_ColorGREEN);
    gfx::Size display_size = GetPrimaryDisplay().size();
    // Simplifies rendering to be consistent for pixel testing.
    SetDecodedPhotoSize(display_size.width(), display_size.height());
    // Set a very long photo refresh interval so the view we are looking
    // at does not animate out while screen capturing. By default two photo
    // views animate in/out to transition between photos every 60 seconds.
    AmbientUiModel::Get()->SetPhotoRefreshInterval(base::Hours(1));
    // Stop the clock and media info from moving around.
    DisableJitter();
  }

  void TearDown() override {
    CloseAmbientScreen();
    AmbientAshTestBase::TearDown();
  }
};

INSTANTIATE_TEST_SUITE_P(
    All,
    AmbientSlideshowPixelTest,
    testing::Combine(::testing::ValuesIn(GetMediaMedataVariations()),
                     testing::Bool(),
                     testing::Bool()),
    &GetName);

TEST_P(AmbientSlideshowPixelTest, ShowMediaStringView) {
  SetAmbientShownAndWaitForWidgets();

  const absl::optional<media_session::MediaMetadata>& media_metadata =
      GetMediaMetadata(GetParam());

  if (media_metadata.has_value()) {
    SimulateMediaMetadataChanged(media_metadata.value());
    SimulateMediaPlaybackStateChanged(
        media_session::mojom::MediaPlaybackState::kPlaying);
  }

  EXPECT_TRUE(GetPixelDiffer()->CompareUiComponentsOnPrimaryScreen(
      "AmbientSlideshow", /*revision_number=*/0,
      ash::Shell::GetPrimaryRootWindow()));
}

}  // namespace ash

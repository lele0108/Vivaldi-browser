// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_page_handler.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/new_tab_page/chrome_colors/chrome_colors_factory.h"
#include "chrome/browser/new_tab_page/chrome_colors/chrome_colors_service.h"
#include "chrome/browser/new_tab_page/chrome_colors/generated_colors_info.h"
#include "chrome/browser/new_tab_page/chrome_colors/selected_colors_info.h"
#include "chrome/browser/search/background/ntp_background_data.h"
#include "chrome/browser/search/background/ntp_background_service_factory.h"
#include "chrome/browser/search/background/ntp_custom_background_service.h"
#include "chrome/browser/search/background/ntp_custom_background_service_observer.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/webui/new_tab_page/ntp_pref_names.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome.mojom.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_colors.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_section.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_service.h"
#include "components/search/ntp_features.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_web_contents_factory.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/dynamic_color/palette_factory.h"
#include "ui/native_theme/native_theme.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace {

using testing::_;

// A test SelectFileDialog to go straight to calling the listener.
class TestSelectFileDialog : public ui::SelectFileDialog {
 public:
  TestSelectFileDialog(Listener* listener,
                       std::unique_ptr<ui::SelectFilePolicy> policy,
                       const bool auto_cancel)
      : ui::SelectFileDialog(listener, std::move(policy)),
        auto_cancel_(auto_cancel) {}

  TestSelectFileDialog(const TestSelectFileDialog&) = delete;
  TestSelectFileDialog& operator=(const TestSelectFileDialog&) = delete;

 protected:
  ~TestSelectFileDialog() override = default;

  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      void* params,
                      const GURL* caller) override {
    if (auto_cancel_) {
      listener_->FileSelectionCanceled(params);
    } else {
      listener_->FileSelected(base::FilePath(FILE_PATH_LITERAL("/test/path")),
                              file_type_index, params);
    }
  }
  // Pure virtual methods that need to be implemented.
  bool IsRunning(gfx::NativeWindow owning_window) const override {
    return false;
  }
  void ListenerDestroyed() override {}
  bool HasMultipleFileTypeChoicesImpl() override { return false; }

 private:
  bool auto_cancel_;
};

class TestSelectFilePolicy : public ui::SelectFilePolicy {
 public:
  TestSelectFilePolicy& operator=(const TestSelectFilePolicy&) = delete;

  // Pure virtual methods that need to be implemented.
  bool CanOpenSelectFileDialog() override { return true; }
  void SelectFileDenied() override {}
};

// A test SelectFileDialogFactory so that the TestSelectFileDialog is used.
class TestSelectFileDialogFactory : public ui::SelectFileDialogFactory {
 public:
  explicit TestSelectFileDialogFactory(bool auto_cancel)
      : auto_cancel_(auto_cancel) {}

  TestSelectFileDialogFactory& operator=(const TestSelectFileDialogFactory&) =
      delete;

  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new TestSelectFileDialog(
        listener, std::make_unique<TestSelectFilePolicy>(), auto_cancel_);
  }

 private:
  bool auto_cancel_;
};

class MockPage : public side_panel::mojom::CustomizeChromePage {
 public:
  MockPage() = default;
  ~MockPage() override = default;

  mojo::PendingRemote<side_panel::mojom::CustomizeChromePage>
  BindAndGetRemote() {
    DCHECK(!receiver_.is_bound());
    return receiver_.BindNewPipeAndPassRemote();
  }

  void FlushForTesting() { receiver_.FlushForTesting(); }

  MOCK_METHOD3(
      SetModulesSettings,
      void(std::vector<side_panel::mojom::ModuleSettingsPtr> modules_settings,
           bool managed,
           bool visible));
  MOCK_METHOD(void,
              SetMostVisitedSettings,
              (bool custom_links_enabled, bool visible));
  MOCK_METHOD(void, SetTheme, (side_panel::mojom::ThemePtr));
  MOCK_METHOD(void,
              ScrollToSection,
              (side_panel::mojom::CustomizeChromeSection));

  mojo::Receiver<side_panel::mojom::CustomizeChromePage> receiver_{this};
};

class MockNtpCustomBackgroundService : public NtpCustomBackgroundService {
 public:
  explicit MockNtpCustomBackgroundService(Profile* profile)
      : NtpCustomBackgroundService(profile) {}
  MOCK_METHOD(absl::optional<CustomBackground>, GetCustomBackground, ());
  MOCK_METHOD(void, ResetCustomBackgroundInfo, ());
  MOCK_METHOD(void, SelectLocalBackgroundImage, (const base::FilePath&));
  MOCK_METHOD(void, AddObserver, (NtpCustomBackgroundServiceObserver*));
  MOCK_METHOD(void,
              SetCustomBackgroundInfo,
              (const GURL&,
               const GURL&,
               const std::string&,
               const std::string&,
               const GURL&,
               const std::string&));
  MOCK_METHOD(bool, IsCustomBackgroundDisabledByPolicy, ());
};

class MockNtpBackgroundService : public NtpBackgroundService {
 public:
  explicit MockNtpBackgroundService(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
      : NtpBackgroundService(url_loader_factory) {}
  MOCK_CONST_METHOD0(collection_info, std::vector<CollectionInfo>&());
  MOCK_CONST_METHOD0(collection_images, std::vector<CollectionImage>&());
  MOCK_METHOD(void, FetchCollectionInfo, ());
  MOCK_METHOD(void, FetchCollectionImageInfo, (const std::string&));
  MOCK_METHOD(void, AddObserver, (NtpBackgroundServiceObserver*));
};

class MockThemeService : public ThemeService {
 public:
  MockThemeService() : ThemeService(nullptr, theme_helper_) { set_ready(); }
  using ThemeService::NotifyThemeChanged;
  MOCK_CONST_METHOD0(UsingDefaultTheme, bool());
  MOCK_CONST_METHOD0(UsingExtensionTheme, bool());
  MOCK_CONST_METHOD0(UsingSystemTheme, bool());
  MOCK_CONST_METHOD0(UsingPolicyTheme, bool());
  MOCK_CONST_METHOD0(GetAutogeneratedThemeColor, SkColor());
  MOCK_CONST_METHOD0(GetThemeID, std::string());
  MOCK_CONST_METHOD0(GetBrowserColorScheme, ThemeService::BrowserColorScheme());
  MOCK_METHOD(void, UseDefaultTheme, ());
  MOCK_METHOD(void, BuildAutogeneratedThemeFromColor, (SkColor));

 private:
  ThemeHelper theme_helper_;
};

std::unique_ptr<TestingProfile> MakeTestingProfile(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  TestingProfile::Builder profile_builder;
  profile_builder.AddTestingFactory(
      NtpBackgroundServiceFactory::GetInstance(),
      base::BindRepeating(
          [](scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
             content::BrowserContext* context)
              -> std::unique_ptr<KeyedService> {
            return std::make_unique<
                testing::NiceMock<MockNtpBackgroundService>>(
                url_loader_factory);
          },
          url_loader_factory));
  profile_builder.AddTestingFactory(
      ThemeServiceFactory::GetInstance(),
      base::BindRepeating([](content::BrowserContext* context)
                              -> std::unique_ptr<KeyedService> {
        return std::make_unique<testing::NiceMock<MockThemeService>>();
      }));
  profile_builder.SetSharedURLLoaderFactory(url_loader_factory);
  auto profile = profile_builder.Build();
  return profile;
}

}  // namespace

class CustomizeChromePageHandlerTest : public testing::Test {
 public:
  CustomizeChromePageHandlerTest()
      : profile_(MakeTestingProfile(
            base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                &test_url_loader_factory_))),
        mock_ntp_custom_background_service_(profile_.get()),
        mock_ntp_background_service_(static_cast<MockNtpBackgroundService*>(
            NtpBackgroundServiceFactory::GetForProfile(profile_.get()))),
        web_contents_(web_contents_factory_.CreateWebContents(profile_.get())),
        mock_theme_service_(static_cast<MockThemeService*>(
            ThemeServiceFactory::GetForProfile(profile_.get()))) {}

  void SetUp() override {
    EXPECT_CALL(mock_ntp_background_service(), AddObserver)
        .Times(1)
        .WillOnce(testing::SaveArg<0>(&ntp_background_service_observer_));
    EXPECT_CALL(mock_ntp_custom_background_service_, AddObserver)
        .Times(1)
        .WillOnce(
            testing::SaveArg<0>(&ntp_custom_background_service_observer_));
    const std::vector<std::pair<const std::string, int>> module_id_names = {
        {"recipe_tasks", IDS_NTP_MODULES_RECIPE_TASKS_SENTENCE},
        {"chrome_cart", IDS_NTP_MODULES_CART_SENTENCE}};
    handler_ = std::make_unique<CustomizeChromePageHandler>(
        mojo::PendingReceiver<side_panel::mojom::CustomizeChromePageHandler>(),
        mock_page_.BindAndGetRemote(), &mock_ntp_custom_background_service_,
        web_contents_, module_id_names);
    mock_page_.FlushForTesting();
    EXPECT_EQ(handler_.get(), ntp_background_service_observer_);
    EXPECT_EQ(handler_.get(), ntp_custom_background_service_observer_);

    browser_window_ = std::make_unique<TestBrowserWindow>();
    Browser::CreateParams browser_params(profile_.get(), true);
    browser_params.type = Browser::TYPE_NORMAL;
    browser_params.window = browser_window_.get();
    browser_ = std::unique_ptr<Browser>(Browser::Create(browser_params));

    scoped_feature_list_.Reset();
  }

  void TearDown() override {
    browser_->tab_strip_model()->CloseAllTabs();
    browser_.reset();
    browser_window_.reset();
  }

  TestingProfile& profile() { return *profile_; }
  content::WebContents& web_contents() { return *web_contents_; }
  CustomizeChromePageHandler& handler() { return *handler_; }
  NtpCustomBackgroundServiceObserver& ntp_custom_background_service_observer() {
    return *ntp_custom_background_service_observer_;
  }
  MockNtpBackgroundService& mock_ntp_background_service() {
    return *mock_ntp_background_service_;
  }
  NtpBackgroundServiceObserver& ntp_background_service_observer() {
    return *ntp_background_service_observer_;
  }
  MockThemeService& mock_theme_service() { return *mock_theme_service_; }
  Browser& browser() { return *browser_; }
  base::HistogramTester& histogram_tester() { return histogram_tester_; }

 protected:
  // NOTE: The initialization order of these members matters.
  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<TestingProfile> profile_;
  testing::NiceMock<MockNtpCustomBackgroundService>
      mock_ntp_custom_background_service_;
  // This field is not a raw_ptr<> because it was filtered by the rewriter for:
  // #addr-of
  RAW_PTR_EXCLUSION NtpCustomBackgroundServiceObserver*
      ntp_custom_background_service_observer_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  raw_ptr<MockNtpBackgroundService> mock_ntp_background_service_;
  content::TestWebContentsFactory web_contents_factory_;
  raw_ptr<content::WebContents> web_contents_;
  testing::NiceMock<MockPage> mock_page_;
  // This field is not a raw_ptr<> because it was filtered by the rewriter for:
  // #addr-of
  RAW_PTR_EXCLUSION NtpBackgroundServiceObserver*
      ntp_background_service_observer_;
  raw_ptr<MockThemeService> mock_theme_service_;
  base::test::ScopedFeatureList scoped_feature_list_;
  std::unique_ptr<Browser> browser_;
  std::unique_ptr<TestBrowserWindow> browser_window_;
  base::HistogramTester histogram_tester_;
  std::unique_ptr<CustomizeChromePageHandler> handler_;
};

TEST_F(CustomizeChromePageHandlerTest, SetMostVisitedSettings) {
  bool custom_links_enabled;
  bool visible;
  EXPECT_CALL(mock_page_, SetMostVisitedSettings)
      .Times(4)
      .WillRepeatedly(
          testing::Invoke([&custom_links_enabled, &visible](
                              bool custom_links_enabled_arg, bool visible_arg) {
            custom_links_enabled = custom_links_enabled_arg;
            visible = visible_arg;
          }));

  profile().GetPrefs()->SetBoolean(ntp_prefs::kNtpUseMostVisitedTiles, false);
  profile().GetPrefs()->SetBoolean(ntp_prefs::kNtpShortcutsVisible, false);

  histogram_tester().ExpectTotalCount("NewTabPage.CustomizeShortcutAction", 0);
  EXPECT_FALSE(
      profile().GetPrefs()->GetBoolean(ntp_prefs::kNtpUseMostVisitedTiles));
  EXPECT_FALSE(
      profile().GetPrefs()->GetBoolean(ntp_prefs::kNtpShortcutsVisible));

  handler().SetMostVisitedSettings(/*custom_links_enabled=*/false,
                                   /*visible=*/true);
  mock_page_.FlushForTesting();

  EXPECT_TRUE(
      profile().GetPrefs()->GetBoolean(ntp_prefs::kNtpUseMostVisitedTiles));
  EXPECT_TRUE(
      profile().GetPrefs()->GetBoolean(ntp_prefs::kNtpShortcutsVisible));
  histogram_tester().ExpectTotalCount("NewTabPage.CustomizeShortcutAction", 2);
}

TEST_F(CustomizeChromePageHandlerTest, GetOverviewChromeColors) {
  std::vector<side_panel::mojom::ChromeColorPtr> colors;
  base::MockCallback<CustomizeChromePageHandler::GetChromeColorsCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&colors](std::vector<side_panel::mojom::ChromeColorPtr> colors_arg) {
            colors = std::move(colors_arg);
          }));
  handler().GetOverviewChromeColors(false, callback.Get());

  ASSERT_EQ(kCustomizeChromeColors.size(), colors.size());
  for (size_t i = 0; i < kCustomizeChromeColors.size(); i++) {
    EXPECT_EQ(l10n_util::GetStringUTF8(kCustomizeChromeColors[i].label_id),
              colors[i]->name);
    EXPECT_EQ(kCustomizeChromeColors[i].color, colors[i]->seed);
    EXPECT_EQ(GetAutogeneratedThemeColors(kCustomizeChromeColors[i].color)
                  .active_tab_color,
              colors[i]->background);
    EXPECT_EQ(GetAutogeneratedThemeColors(kCustomizeChromeColors[i].color)
                  .frame_color,
              colors[i]->foreground);
  }
}

TEST_F(CustomizeChromePageHandlerTest, GetOverviewChromeColorsGM3) {
  scoped_feature_list_.InitWithFeatures(
      {features::kChromeRefresh2023, features::kChromeWebuiRefresh2023}, {});
  std::vector<side_panel::mojom::ChromeColorPtr> colors;
  base::MockCallback<CustomizeChromePageHandler::GetChromeColorsCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::_))
      .Times(2)
      .WillRepeatedly(testing::Invoke(
          [&colors](std::vector<side_panel::mojom::ChromeColorPtr> colors_arg) {
            colors = std::move(colors_arg);
          }));

  // Test with Dark Mode on.
  handler().GetOverviewChromeColors(true, callback.Get());

  ASSERT_EQ(kCustomizeChromeColors.size(), colors.size());
  for (size_t i = 0; i < kCustomizeChromeColors.size(); i++) {
    auto palette = ui::GeneratePalette(
        kCustomizeChromeColors[i].color,
        ui::ColorProviderManager::SchemeVariant::kTonalSpot);
    EXPECT_EQ(l10n_util::GetStringUTF8(kCustomizeChromeColors[i].label_id),
              colors[i]->name);
    EXPECT_EQ(kCustomizeChromeColors[i].color, colors[i]->seed);
    EXPECT_EQ(palette->primary().get(80), colors[i]->background);
    EXPECT_EQ(palette->primary().get(30), colors[i]->foreground);
    EXPECT_EQ(palette->secondary().get(50), colors[i]->base);
  }

  // Test with Dark Mode off.
  handler().GetOverviewChromeColors(false, callback.Get());

  ASSERT_EQ(kCustomizeChromeColors.size(), colors.size());
  for (size_t i = 0; i < kCustomizeChromeColors.size(); i++) {
    auto palette = ui::GeneratePalette(
        kCustomizeChromeColors[i].color,
        ui::ColorProviderManager::SchemeVariant::kTonalSpot);
    EXPECT_EQ(l10n_util::GetStringUTF8(kCustomizeChromeColors[i].label_id),
              colors[i]->name);
    EXPECT_EQ(kCustomizeChromeColors[i].color, colors[i]->seed);
    EXPECT_EQ(palette->primary().get(40), colors[i]->background);
    EXPECT_EQ(palette->primary().get(90), colors[i]->foreground);
    EXPECT_EQ(palette->primary().get(80), colors[i]->base);
  }
}

TEST_F(CustomizeChromePageHandlerTest, GetChromeColors) {
  std::vector<side_panel::mojom::ChromeColorPtr> colors;
  base::MockCallback<CustomizeChromePageHandler::GetChromeColorsCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&colors](std::vector<side_panel::mojom::ChromeColorPtr> colors_arg) {
            colors = std::move(colors_arg);
          }));
  handler().GetChromeColors(callback.Get());

  auto num_colors = sizeof(chrome_colors::kGeneratedColorsInfo) /
                    sizeof(chrome_colors::kGeneratedColorsInfo[0]);
  ASSERT_EQ(num_colors, colors.size());
  for (size_t i = 0; i < num_colors; i++) {
    EXPECT_EQ(l10n_util::GetStringUTF8(
                  chrome_colors::kGeneratedColorsInfo[i].label_id),
              colors[i]->name);
    EXPECT_EQ(chrome_colors::kGeneratedColorsInfo[i].color, colors[i]->seed);
    EXPECT_EQ(GetAutogeneratedThemeColors(
                  chrome_colors::kGeneratedColorsInfo[i].color)
                  .active_tab_color,
              colors[i]->background);
    EXPECT_EQ(GetAutogeneratedThemeColors(
                  chrome_colors::kGeneratedColorsInfo[i].color)
                  .frame_color,
              colors[i]->foreground);
  }
}

enum class ThemeUpdateSource {
  kMojo,
  kThemeService,
  kNativeTheme,
  kCustomBackgroundService,
};

class CustomizeChromePageHandlerSetThemeTest
    : public CustomizeChromePageHandlerTest,
      public ::testing::WithParamInterface<ThemeUpdateSource> {
 public:
  void UpdateTheme() {
    switch (GetParam()) {
      case ThemeUpdateSource::kMojo:
        handler().UpdateTheme();
        break;
      case ThemeUpdateSource::kThemeService:
        mock_theme_service().NotifyThemeChanged();
        break;
      case ThemeUpdateSource::kNativeTheme:
        ui::NativeTheme::GetInstanceForNativeUi()->NotifyOnNativeThemeUpdated();
        break;
      case ThemeUpdateSource::kCustomBackgroundService:
        ntp_custom_background_service_observer()
            .OnCustomBackgroundImageUpdated();
        break;
    }
  }
};

TEST_P(CustomizeChromePageHandlerSetThemeTest, SetTheme) {
  side_panel::mojom::ThemePtr theme;
  EXPECT_CALL(mock_page_, SetTheme)
      .Times(1)
      .WillOnce(testing::Invoke([&theme](side_panel::mojom::ThemePtr arg) {
        theme = std::move(arg);
      }));
  CustomBackground custom_background;
  custom_background.custom_background_url = GURL("https://foo.com/img.png");
  custom_background.custom_background_attribution_line_1 = "foo line";
  custom_background.is_uploaded_image = false;
  custom_background.custom_background_main_color = SK_ColorGREEN;
  custom_background.collection_id = "test_collection";
  custom_background.daily_refresh_enabled = false;
  ON_CALL(mock_ntp_custom_background_service_, GetCustomBackground())
      .WillByDefault(testing::Return(absl::make_optional(custom_background)));
  ON_CALL(mock_theme_service(), GetAutogeneratedThemeColor())
      .WillByDefault(testing::Return(SK_ColorBLUE));
  ON_CALL(mock_theme_service(), UsingDefaultTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingSystemTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingPolicyTheme())
      .WillByDefault(testing::Return(true));
  ON_CALL(mock_ntp_custom_background_service_,
          IsCustomBackgroundDisabledByPolicy())
      .WillByDefault(testing::Return(true));
  ui::NativeTheme::GetInstanceForNativeUi()->set_use_dark_colors(true);

  UpdateTheme();
  mock_page_.FlushForTesting();

  ASSERT_TRUE(theme);
  ASSERT_TRUE(theme->background_image);
  EXPECT_EQ("https://foo.com/img.png", theme->background_image->url);
  EXPECT_FALSE(theme->background_image->is_uploaded_image);
  EXPECT_FALSE(theme->background_image->daily_refresh_enabled);
  EXPECT_EQ(SK_ColorGREEN,
            theme->background_image->main_color.value_or(SK_ColorWHITE));
  EXPECT_EQ("foo line", theme->background_image->title);
  EXPECT_EQ("test_collection", theme->background_image->collection_id);
  EXPECT_TRUE(theme->is_dark_mode);
  EXPECT_EQ(SK_ColorBLUE, theme->seed_color);
  EXPECT_EQ(
      web_contents().GetColorProvider().GetColor(kColorNewTabPageBackground),
      theme->background_color);
  EXPECT_EQ(web_contents().GetColorProvider().GetColor(ui::kColorFrameActive),
            theme->foreground_color);
  EXPECT_EQ(web_contents().GetColorProvider().GetColor(kColorNewTabPageText),
            theme->color_picker_icon_color);
  EXPECT_TRUE(theme->colors_managed_by_policy);
  EXPECT_TRUE(theme->background_managed_by_policy);
}

TEST_P(CustomizeChromePageHandlerSetThemeTest, SetThemeColorSchemeGM3) {
  scoped_feature_list_.InitWithFeatures(
      {features::kChromeRefresh2023, features::kChromeWebuiRefresh2023}, {});
  side_panel::mojom::ThemePtr theme;
  EXPECT_CALL(mock_page_, SetTheme)
      .Times(2)
      .WillRepeatedly(
          testing::Invoke([&theme](side_panel::mojom::ThemePtr arg) {
            theme = std::move(arg);
          }));

  // Set mocks needed for UpdateTheme.
  CustomBackground custom_background;
  custom_background.custom_background_url = GURL("https://foo.com/img.png");
  custom_background.custom_background_attribution_line_1 = "foo line";
  custom_background.is_uploaded_image = false;
  custom_background.custom_background_main_color = SK_ColorGREEN;
  custom_background.collection_id = "test_collection";
  custom_background.daily_refresh_enabled = false;
  ON_CALL(mock_ntp_custom_background_service_, GetCustomBackground())
      .WillByDefault(testing::Return(absl::make_optional(custom_background)));
  ON_CALL(mock_theme_service(), GetAutogeneratedThemeColor())
      .WillByDefault(testing::Return(SK_ColorBLUE));
  ON_CALL(mock_theme_service(), UsingDefaultTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingSystemTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingPolicyTheme())
      .WillByDefault(testing::Return(true));
  ON_CALL(mock_ntp_custom_background_service_,
          IsCustomBackgroundDisabledByPolicy())
      .WillByDefault(testing::Return(true));

  // Set BrowserColorScheme to System and system to dark mode.
  ON_CALL(mock_theme_service(), GetBrowserColorScheme())
      .WillByDefault(
          testing::Return(ThemeService::BrowserColorScheme::kSystem));
  ui::NativeTheme::GetInstanceForNativeUi()->set_use_dark_colors(true);

  UpdateTheme();
  mock_page_.FlushForTesting();

  // // Theme should be dark to match the system.
  EXPECT_TRUE(theme->is_dark_mode);

  // Set BrowserColorScheme to Light and leave system still on dark mode.
  ON_CALL(mock_theme_service(), GetBrowserColorScheme())
      .WillByDefault(testing::Return(ThemeService::BrowserColorScheme::kLight));

  UpdateTheme();
  mock_page_.FlushForTesting();

  EXPECT_FALSE(theme->is_dark_mode);
}

TEST_P(CustomizeChromePageHandlerSetThemeTest, SetThemeWithDailyRefresh) {
  side_panel::mojom::ThemePtr theme;
  EXPECT_CALL(mock_page_, SetTheme)
      .Times(1)
      .WillOnce(testing::Invoke([&theme](side_panel::mojom::ThemePtr arg) {
        theme = std::move(arg);
      }));
  CustomBackground custom_background;
  custom_background.custom_background_url = GURL("https://foo.com/img.png");
  custom_background.daily_refresh_enabled = true;
  custom_background.collection_id = "test_collection";
  ON_CALL(mock_ntp_custom_background_service_, GetCustomBackground())
      .WillByDefault(testing::Return(absl::make_optional(custom_background)));

  UpdateTheme();
  mock_page_.FlushForTesting();

  ASSERT_TRUE(theme);
  ASSERT_TRUE(theme->background_image);
  EXPECT_TRUE(theme->background_image->daily_refresh_enabled);
  EXPECT_EQ("test_collection", theme->background_image->collection_id);
}

TEST_P(CustomizeChromePageHandlerSetThemeTest, SetUploadedImage) {
  side_panel::mojom::ThemePtr theme;
  EXPECT_CALL(mock_page_, SetTheme)
      .Times(1)
      .WillOnce(testing::Invoke([&theme](side_panel::mojom::ThemePtr arg) {
        theme = std::move(arg);
      }));
  CustomBackground custom_background;
  custom_background.custom_background_url = GURL("https://foo.com/img.png");
  custom_background.is_uploaded_image = true;
  ON_CALL(mock_ntp_custom_background_service_, GetCustomBackground())
      .WillByDefault(testing::Return(absl::make_optional(custom_background)));
  ON_CALL(mock_theme_service(), UsingDefaultTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingSystemTheme())
      .WillByDefault(testing::Return(false));

  UpdateTheme();
  mock_page_.FlushForTesting();

  ASSERT_TRUE(theme);
  ASSERT_TRUE(theme->background_image);
  EXPECT_EQ("https://foo.com/img.png", theme->background_image->url);
  ASSERT_TRUE(theme->background_image->is_uploaded_image);
}

TEST_P(CustomizeChromePageHandlerSetThemeTest, SetThirdPartyTheme) {
  side_panel::mojom::ThemePtr theme;
  EXPECT_CALL(mock_page_, SetTheme)
      .Times(1)
      .WillOnce(testing::Invoke([&theme](side_panel::mojom::ThemePtr arg) {
        theme = std::move(arg);
      }));
  CustomBackground custom_background;
  custom_background.custom_background_url = GURL("https://foo.com/img.png");

  auto* extension_registry = extensions::ExtensionRegistry::Get(profile_.get());
  scoped_refptr<const extensions::Extension> extension;
  extension = extensions::ExtensionBuilder()
                  .SetManifest(base::Value::Dict()
                                   .Set("name", "Foo Extension")
                                   .Set("version", "1.0.0")
                                   .Set("manifest_version", 2))
                  .SetID("foo")
                  .Build();
  extension_registry->AddEnabled(extension);

  ON_CALL(mock_ntp_custom_background_service_, GetCustomBackground())
      .WillByDefault(testing::Return(absl::make_optional(custom_background)));
  ON_CALL(mock_theme_service(), UsingDefaultTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), UsingExtensionTheme())
      .WillByDefault(testing::Return(true));
  ON_CALL(mock_theme_service(), UsingSystemTheme())
      .WillByDefault(testing::Return(false));
  ON_CALL(mock_theme_service(), GetThemeID())
      .WillByDefault(testing::Return("foo"));

  UpdateTheme();
  mock_page_.FlushForTesting();

  ASSERT_TRUE(theme);
  ASSERT_TRUE(theme->background_image);
  EXPECT_EQ("https://foo.com/img.png", theme->background_image->url);
  ASSERT_TRUE(theme->third_party_theme_info);
  EXPECT_EQ("foo", theme->third_party_theme_info->id);
  EXPECT_EQ("Foo Extension", theme->third_party_theme_info->name);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    CustomizeChromePageHandlerSetThemeTest,
    ::testing::Values(ThemeUpdateSource::kMojo,
                      ThemeUpdateSource::kThemeService,
                      ThemeUpdateSource::kNativeTheme,
                      ThemeUpdateSource::kCustomBackgroundService));

TEST_F(CustomizeChromePageHandlerTest, GetBackgroundCollections) {
  std::vector<CollectionInfo> test_collection_info;
  CollectionInfo test_collection;
  test_collection.collection_id = "test_id";
  test_collection.collection_name = "test_name";
  test_collection.preview_image_url = GURL("https://test.jpg");
  test_collection_info.push_back(test_collection);
  ON_CALL(mock_ntp_background_service(), collection_info())
      .WillByDefault(testing::ReturnRef(test_collection_info));

  std::vector<side_panel::mojom::BackgroundCollectionPtr> collections;
  base::MockCallback<
      CustomizeChromePageHandler::GetBackgroundCollectionsCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&collections](std::vector<side_panel::mojom::BackgroundCollectionPtr>
                             collections_arg) {
            collections = std::move(collections_arg);
          }));
  EXPECT_CALL(mock_ntp_background_service(), FetchCollectionInfo).Times(1);
  handler().GetBackgroundCollections(callback.Get());
  ntp_background_service_observer().OnCollectionInfoAvailable();

  EXPECT_EQ(collections.size(), test_collection_info.size());
  EXPECT_EQ(test_collection_info[0].collection_id, collections[0]->id);
  EXPECT_EQ(test_collection_info[0].collection_name, collections[0]->label);
  EXPECT_EQ(test_collection_info[0].preview_image_url,
            collections[0]->preview_image_url);
}

TEST_F(CustomizeChromePageHandlerTest, GetBackgroundImages) {
  std::vector<CollectionImage> test_collection_images;
  CollectionImage test_image;
  std::vector<std::string> attribution{"test1", "test2"};
  test_image.attribution = attribution;
  test_image.attribution_action_url = GURL("https://action.com");
  test_image.image_url = GURL("https://test_image.jpg");
  test_image.thumbnail_image_url = GURL("https://test_thumbnail.jpg");
  test_collection_images.push_back(test_image);
  ON_CALL(mock_ntp_background_service(), collection_images())
      .WillByDefault(testing::ReturnRef(test_collection_images));

  std::vector<side_panel::mojom::CollectionImagePtr> images;
  base::MockCallback<CustomizeChromePageHandler::GetBackgroundImagesCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&images](
              std::vector<side_panel::mojom::CollectionImagePtr> images_arg) {
            images = std::move(images_arg);
          }));
  EXPECT_CALL(mock_ntp_background_service(), FetchCollectionImageInfo).Times(1);
  handler().GetBackgroundImages("test_id", callback.Get());
  ntp_background_service_observer().OnCollectionImagesAvailable();

  EXPECT_EQ(images.size(), test_collection_images.size());
  EXPECT_EQ(test_collection_images[0].attribution[0], images[0]->attribution_1);
  EXPECT_EQ(test_collection_images[0].attribution[1], images[0]->attribution_2);
  EXPECT_EQ(test_collection_images[0].attribution_action_url,
            images[0]->attribution_url);
  EXPECT_EQ(test_collection_images[0].image_url, images[0]->image_url);
  EXPECT_EQ(test_collection_images[0].thumbnail_image_url,
            images[0]->preview_image_url);
}

TEST_F(CustomizeChromePageHandlerTest, SetDefaultColor) {
  EXPECT_CALL(mock_theme_service(), UseDefaultTheme).Times(1);

  handler().SetDefaultColor();
}

TEST_F(CustomizeChromePageHandlerTest, SetSeedColor) {
  SkColor color = SK_ColorWHITE;
  EXPECT_CALL(mock_theme_service(), BuildAutogeneratedThemeFromColor)
      .Times(1)
      .WillOnce(testing::SaveArg<0>(&color));

  handler().SetSeedColor(SK_ColorBLUE);

  EXPECT_EQ(SK_ColorBLUE, color);
}

TEST_F(CustomizeChromePageHandlerTest, RemoveBackgroundImage) {
  EXPECT_CALL(mock_ntp_custom_background_service_, ResetCustomBackgroundInfo)
      .Times(1);

  handler().RemoveBackgroundImage();
}

TEST_F(CustomizeChromePageHandlerTest, ChooseLocalCustomBackgroundSuccess) {
  bool success;
  base::MockCallback<
      CustomizeChromePageHandler::ChooseLocalCustomBackgroundCallback>
      callback;
  ui::SelectFileDialog::SetFactory(
      std::make_unique<TestSelectFileDialogFactory>(false));
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&success](bool success_arg) { success = std::move(success_arg); }));
  EXPECT_CALL(mock_ntp_custom_background_service_, SelectLocalBackgroundImage)
      .Times(1);
  EXPECT_CALL(mock_theme_service(), UseDefaultTheme).Times(1);
  handler().ChooseLocalCustomBackground(callback.Get());
  EXPECT_TRUE(success);
}

TEST_F(CustomizeChromePageHandlerTest, ChooseLocalCustomBackgroundCancel) {
  bool success;
  base::MockCallback<
      CustomizeChromePageHandler::ChooseLocalCustomBackgroundCallback>
      callback;
  ui::SelectFileDialog::SetFactory(
      std::make_unique<TestSelectFileDialogFactory>(true));
  EXPECT_CALL(callback, Run(testing::_))
      .Times(1)
      .WillOnce(testing::Invoke(
          [&success](bool success_arg) { success = std::move(success_arg); }));
  handler().ChooseLocalCustomBackground(callback.Get());
  EXPECT_TRUE(!success);
}

TEST_F(CustomizeChromePageHandlerTest, SetBackgroundImage) {
  EXPECT_CALL(mock_ntp_custom_background_service_, SetCustomBackgroundInfo)
      .Times(1);
  handler().SetBackgroundImage(
      "attribution1", "attribution2", GURL("https://attribution.com"),
      GURL("https://image.jpg"), GURL("https://thumbnail.jpg"), "collectionId");
}

TEST_F(CustomizeChromePageHandlerTest, OpenChromeWebStore) {
  histogram_tester().ExpectTotalCount("NewTabPage.ChromeWebStoreOpen", 0);
  handler().OpenChromeWebStore();
  ASSERT_EQ(1, browser().tab_strip_model()->count());
  ASSERT_EQ("https://chrome.google.com/webstore?category=theme",
            browser().tab_strip_model()->GetWebContentsAt(0)->GetURL());
  histogram_tester().ExpectTotalCount("NewTabPage.ChromeWebStoreOpen", 1);

  ASSERT_EQ(
      histogram_tester().GetBucketCount("NewTabPage.ChromeWebStoreOpen",
                                        NtpChromeWebStoreOpen::kAppearance),
      1);
}

TEST_F(CustomizeChromePageHandlerTest, OpenThirdPartyThemePage) {
  histogram_tester().ExpectTotalCount("NewTabPage.ChromeWebStoreOpen", 0);
  handler().OpenThirdPartyThemePage("foo");
  ASSERT_EQ(1, browser().tab_strip_model()->count());
  ASSERT_EQ("https://chrome.google.com/webstore/detail/foo",
            browser().tab_strip_model()->GetWebContentsAt(0)->GetURL());
  histogram_tester().ExpectTotalCount("NewTabPage.ChromeWebStoreOpen", 1);
  ASSERT_EQ(
      histogram_tester().GetBucketCount("NewTabPage.ChromeWebStoreOpen",
                                        NtpChromeWebStoreOpen::kCollections),
      1);
}

TEST_F(CustomizeChromePageHandlerTest, SetDailyRefreshCollectionId) {
  EXPECT_CALL(mock_ntp_custom_background_service_, SetCustomBackgroundInfo)
      .Times(1);
  handler().SetDailyRefreshCollectionId("test_id");
}

TEST_F(CustomizeChromePageHandlerTest, ScrollToSection) {
  side_panel::mojom::CustomizeChromeSection section;
  EXPECT_CALL(mock_page_, ScrollToSection)
      .Times(1)
      .WillOnce(testing::SaveArg<0>(&section));

  handler().ScrollToSection(CustomizeChromeSection::kAppearance);
  mock_page_.FlushForTesting();

  EXPECT_EQ(side_panel::mojom::CustomizeChromeSection::kAppearance, section);
}

TEST_F(CustomizeChromePageHandlerTest, ScrollToUnspecifiedSection) {
  EXPECT_CALL(mock_page_, ScrollToSection).Times(0);

  handler().ScrollToSection(CustomizeChromeSection::kUnspecified);
  mock_page_.FlushForTesting();
}

TEST_F(CustomizeChromePageHandlerTest, UpdateScrollToSection) {
  side_panel::mojom::CustomizeChromeSection section;
  EXPECT_CALL(mock_page_, ScrollToSection)
      .Times(2)
      .WillRepeatedly(testing::SaveArg<0>(&section));

  handler().ScrollToSection(CustomizeChromeSection::kAppearance);
  handler().UpdateScrollToSection();
  mock_page_.FlushForTesting();

  EXPECT_EQ(side_panel::mojom::CustomizeChromeSection::kAppearance, section);
}

class CustomizeChromePageHandlerWithModulesTest
    : public CustomizeChromePageHandlerTest {
 public:
  void SetUp() override {
    base::test::ScopedFeatureList features;
    features.InitWithFeatures(
        /*enabled_features=*/{ntp_features::kNtpRecipeTasksModule,
                              ntp_features::kNtpChromeCartModule},
        /*disabled_features=*/{});
    CustomizeChromePageHandlerTest::SetUp();
  }
};

TEST_F(CustomizeChromePageHandlerWithModulesTest, SetModulesSettings) {
  std::vector<side_panel::mojom::ModuleSettingsPtr> modules_settings;
  bool managed;
  bool visible;
  EXPECT_CALL(mock_page_, SetModulesSettings)
      .Times(2)
      .WillRepeatedly(
          testing::Invoke([&modules_settings, &managed, &visible](
                              std::vector<side_panel::mojom::ModuleSettingsPtr>
                                  modules_settings_arg,
                              bool managed_arg, bool visible_arg) {
            modules_settings = std::move(modules_settings_arg);
            managed = managed_arg;
            visible = visible_arg;
          }));

  constexpr char kChromeCartId[] = "chrome_cart";
  profile().GetPrefs()->SetBoolean(prefs::kNtpModulesVisible, true);
  auto disabled_module_ids = base::Value::List();
  disabled_module_ids.Append(kChromeCartId);
  profile().GetPrefs()->SetList(prefs::kNtpDisabledModules,
                                std::move(disabled_module_ids));
  mock_page_.FlushForTesting();

  EXPECT_TRUE(visible);
  EXPECT_FALSE(managed);
  EXPECT_EQ(2u, modules_settings.size());
  EXPECT_EQ("recipe_tasks", modules_settings[0]->id);
  EXPECT_TRUE(modules_settings[0]->enabled);
  EXPECT_EQ(kChromeCartId, modules_settings[1]->id);
  EXPECT_FALSE(modules_settings[1]->enabled);
}

TEST_F(CustomizeChromePageHandlerWithModulesTest, SetModulesVisible) {
  profile().GetPrefs()->SetBoolean(prefs::kNtpModulesVisible, false);
  handler().SetModulesVisible(true);

  EXPECT_CALL(mock_page_, SetModulesSettings).Times(2);
  mock_page_.FlushForTesting();

  EXPECT_TRUE(profile().GetPrefs()->GetBoolean(prefs::kNtpModulesVisible));
}

TEST_F(CustomizeChromePageHandlerWithModulesTest, SetModuleDisabled) {
  const std::string kDriveModuleId = "drive";
  handler().SetModuleDisabled(kDriveModuleId, true);
  const auto& disabled_module_ids =
      profile().GetPrefs()->GetList(prefs::kNtpDisabledModules);

  EXPECT_CALL(mock_page_, SetModulesSettings).Times(1);
  mock_page_.FlushForTesting();

  EXPECT_EQ(1u, disabled_module_ids.size());
  EXPECT_EQ(kDriveModuleId, disabled_module_ids.front().GetString());
}

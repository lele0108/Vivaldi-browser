// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/clipboard/clipboard_history_item.h"

#include "ash/clipboard/clipboard_history_util.h"
#include "ash/clipboard/test_support/clipboard_history_item_builder.h"
#include "ash/test/ash_test_base.h"
#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "base/test/icu_test_util.h"
#include "base/test/mock_callback.h"
#include "chromeos/crosapi/mojom/clipboard_history.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/clipboard/clipboard_data.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/strings/grit/ui_strings.h"

namespace ash {

namespace {

struct FormatPair {
  ui::ClipboardInternalFormat clipboard_format;
  crosapi::mojom::ClipboardHistoryDisplayFormat display_format;
};

}  // namespace

using ClipboardHistoryItemTest = AshTestBase;

TEST_F(ClipboardHistoryItemTest, DisplayText) {
  base::test::ScopedRestoreICUDefaultLocale locale("en_US");

  // Populate a builder with all the data formats that we expect to handle.
  ClipboardHistoryItemBuilder builder;
  builder.SetText("Text")
      .SetMarkup("HTML with no image or table tags")
      .SetRtf("Rtf")
      .SetFilenames({ui::FileInfo(base::FilePath("/path/to/File.txt"),
                                  base::FilePath("File.txt")),
                     ui::FileInfo(base::FilePath("/path/to/Other%20File.txt"),
                                  base::FilePath("Other%20File.txt"))})
      .SetBookmarkTitle("Bookmark Title")
      .SetPng(gfx::test::CreatePNGBytes(10))
      .SetFileSystemData({u"/path/to/File.txt", u"/path/to/Other%20File.txt"})
      .SetWebSmartPaste(true);

  // PNG data always takes precedence. When we must show text rather than the
  // image itself, we display the PNG placeholder text.
  EXPECT_EQ(builder.Build().display_text(),
            l10n_util::GetStringUTF16(IDS_CLIPBOARD_MENU_IMAGE));

  builder.ClearPng();

  // In the absence of PNG data, HTML data takes precedence, but we use the
  // plain-text format for the label.
  EXPECT_EQ(builder.Build().display_text(), u"Text");

  builder.ClearText();

  // If plain text does not exist, we show a placeholder label.
  EXPECT_EQ(builder.Build().display_text(),
            l10n_util::GetStringUTF16(IDS_CLIPBOARD_MENU_HTML));

  builder.SetText("Text");

  builder.ClearMarkup();

  // In the absence of HTML data, text data takes precedence.
  EXPECT_EQ(builder.Build().display_text(), u"Text");

  builder.ClearText();

  // In the absence of text data, RTF data takes precedence.
  EXPECT_EQ(builder.Build().display_text(),
            l10n_util::GetStringUTF16(IDS_CLIPBOARD_MENU_RTF_CONTENT));

  builder.ClearRtf();

  // In the absence of RTF data, filename data takes precedence.
  EXPECT_EQ(builder.Build().display_text(), u"File.txt, Other File.txt");

  builder.ClearFilenames();

  // In the absence of filename data, bookmark data takes precedence.
  EXPECT_EQ(builder.Build().display_text(), u"Bookmark Title");

  builder.ClearBookmarkTitle();

  // In the absence of bookmark data, web smart paste data takes precedence.
  EXPECT_EQ(builder.Build().display_text(),
            l10n_util::GetStringUTF16(IDS_CLIPBOARD_MENU_WEB_SMART_PASTE));

  builder.ClearWebSmartPaste();

  // In the absence of web smart paste data, file system data takes precedence.
  // NOTE: File system data is the only kind of custom data currently supported.
  EXPECT_EQ(builder.Build().display_text(), u"File.txt, Other File.txt");
}

// Verifies that a callback can be added to an item to run iff the item's
// display image is updated.
TEST_F(ClipboardHistoryItemTest, SetDisplayImageNotifiesCallback) {
  // Create a clipboard history item.
  ClipboardHistoryItemBuilder builder;
  builder.SetFormat(ui::ClipboardInternalFormat::kHtml);
  ClipboardHistoryItem item = builder.Build();
  EXPECT_EQ(item.display_format(),
            crosapi::mojom::ClipboardHistoryDisplayFormat::kHtml);
  ASSERT_TRUE(item.display_image().has_value());
  EXPECT_EQ(item.display_image().value(),
            clipboard_history_util::GetHtmlPreviewPlaceholder());

  {
    SCOPED_TRACE("Set a callback that is run.");
    // Set a callback to be notified when the item's display image is updated.
    base::MockCallback<base::RepeatingClosure> callback;
    auto subscription = item.AddDisplayImageUpdatedCallback(callback.Get());
    EXPECT_CALL(callback, Run());

    // Update the item's display image. The callback should be run.
    item.SetDisplayImage(ui::ImageModel::FromImage(gfx::test::CreateImage()));

    // Verify that the display image was, in fact, updated.
    EXPECT_NE(item.display_image().value(),
              clipboard_history_util::GetHtmlPreviewPlaceholder());
  }

  {
    SCOPED_TRACE("Set a callback that is not run because the item was copied.");
    base::MockCallback<base::RepeatingClosure> callback;
    auto subscription = item.AddDisplayImageUpdatedCallback(callback.Get());
    EXPECT_CALL(callback, Run()).Times(0);

    // Copy the item and update the new item's display image. The callback
    // should not be run.
    ClipboardHistoryItem copied_item(item);
    copied_item.SetDisplayImage(
        ui::ImageModel::FromImage(gfx::test::CreateImage()));
  }

  {
    SCOPED_TRACE("Set a callback that is not run because the item was moved.");
    base::MockCallback<base::RepeatingClosure> callback;
    auto subscription = item.AddDisplayImageUpdatedCallback(callback.Get());
    EXPECT_CALL(callback, Run()).Times(0);

    // Move the item and update the new item's display image. The callback
    // should not be run.
    ClipboardHistoryItem moved_item(std::move(item));
    moved_item.SetDisplayImage(
        ui::ImageModel::FromImage(gfx::test::CreateImage()));
  }
}

class ClipboardHistoryItemDisplayTest
    : public ClipboardHistoryItemTest,
      public testing::WithParamInterface<FormatPair> {
 public:
  ClipboardHistoryItemDisplayTest() : item_(BuildClipboardHistoryItem()) {}
  ~ClipboardHistoryItemDisplayTest() override = default;

  ui::ClipboardInternalFormat GetClipboardFormat() const {
    return GetParam().clipboard_format;
  }
  crosapi::mojom::ClipboardHistoryDisplayFormat GetDisplayFormat() const {
    return GetParam().display_format;
  }

  const ClipboardHistoryItem& item() const { return item_; }

 private:
  ClipboardHistoryItem BuildClipboardHistoryItem() const {
    ClipboardHistoryItemBuilder builder;
    builder.SetFormat(GetClipboardFormat());
    ClipboardHistoryItem item = builder.Build();
    EXPECT_EQ(item.display_format(), GetDisplayFormat());
    return item;
  }

  const ClipboardHistoryItem item_;
};

INSTANTIATE_TEST_SUITE_P(
    All,
    ClipboardHistoryItemDisplayTest,
    ::testing::Values(
        FormatPair{ui::ClipboardInternalFormat::kText,
                   crosapi::mojom::ClipboardHistoryDisplayFormat::kText},
        FormatPair{ui::ClipboardInternalFormat::kPng,
                   crosapi::mojom::ClipboardHistoryDisplayFormat::kPng},
        FormatPair{ui::ClipboardInternalFormat::kHtml,
                   crosapi::mojom::ClipboardHistoryDisplayFormat::kHtml},
        FormatPair{ui::ClipboardInternalFormat::kFilenames,
                   crosapi::mojom::ClipboardHistoryDisplayFormat::kFile}));

TEST_P(ClipboardHistoryItemDisplayTest, Icon) {
  const auto& maybe_icon = item().icon();
  if (GetDisplayFormat() ==
      crosapi::mojom::ClipboardHistoryDisplayFormat::kFile) {
    ASSERT_TRUE(maybe_icon.has_value());
    EXPECT_TRUE(maybe_icon.value().IsVectorIcon());
  } else {
    EXPECT_FALSE(maybe_icon.has_value());
  }
}

TEST_P(ClipboardHistoryItemDisplayTest, DisplayImage) {
  const auto& maybe_image = item().display_image();
  switch (GetDisplayFormat()) {
    case crosapi::mojom::ClipboardHistoryDisplayFormat::kUnknown:
      NOTREACHED_NORETURN();
    case crosapi::mojom::ClipboardHistoryDisplayFormat::kText:
    case crosapi::mojom::ClipboardHistoryDisplayFormat::kFile:
      EXPECT_FALSE(maybe_image);
      break;
    case crosapi::mojom::ClipboardHistoryDisplayFormat::kPng:
    case crosapi::mojom::ClipboardHistoryDisplayFormat::kHtml:
      ASSERT_TRUE(maybe_image);
      EXPECT_TRUE(maybe_image.value().IsImage());
      break;
  }
}

}  // namespace ash

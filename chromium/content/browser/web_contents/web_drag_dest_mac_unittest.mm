// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "content/browser/web_contents/web_drag_dest_mac.h"

#include <AppKit/AppKit.h>

#include "base/mac/mac_util.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "base/memory/ref_counted.h"
#include "base/strings/sys_string_conversions.h"
#include "content/public/common/drop_data.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "ui/base/clipboard/clipboard_util_mac.h"
#import "ui/base/test/cocoa_helper.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

class WebDragDestTest : public content::RenderViewHostImplTestHarness {
 public:
  void SetUp() override {
    content::RenderViewHostImplTestHarness::SetUp();
    drag_dest_ = [[WebDragDest alloc] initWithWebContentsImpl:contents()];
  }

  base::mac::ScopedNSAutoreleasePool pool_;
  WebDragDest* __strong drag_dest_;
};

// Make sure nothing leaks.
TEST_F(WebDragDestTest, Init) {
  EXPECT_TRUE(drag_dest_);
}

TEST_F(WebDragDestTest, Data) {
  scoped_refptr<ui::UniquePasteboard> pboard = new ui::UniquePasteboard;
  NSString* html_string = @"<html><body><b>hi there</b></body></html>";
  NSString* text_string = @"hi there";
  [pboard->get() setString:@"http://www.google.com"
                   forType:NSPasteboardTypeURL];
  [pboard->get() setString:html_string forType:NSPasteboardTypeHTML];
  [pboard->get() setString:text_string forType:NSPasteboardTypeString];

  content::DropData data =
      content::PopulateDropDataFromPasteboard(pboard->get());

  EXPECT_EQ(data.url.spec(), "http://www.google.com/");
  EXPECT_EQ(base::SysNSStringToUTF16(text_string), data.text);
  EXPECT_EQ(base::SysNSStringToUTF16(html_string), data.html);
}

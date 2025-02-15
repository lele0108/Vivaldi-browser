// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TEST_POPUP_TEST_BASE_H_
#define CHROME_BROWSER_UI_TEST_POPUP_TEST_BASE_H_

#include <string>

#include "chrome/test/base/in_process_browser_test.h"

namespace content {
class ToRenderFrameHost;
}

namespace display {
class Display;
}

// Supports browser tests of popup windows created with window.open().
// https://html.spec.whatwg.org/multipage/nav-history-apis.html#dom-open-dev
class PopupTestBase : public InProcessBrowserTest {
 public:
  PopupTestBase() = default;
  PopupTestBase(const PopupTestBase&) = delete;
  PopupTestBase& operator=(const PopupTestBase&) = delete;

 protected:
  ~PopupTestBase() override = default;

  // InProcessBrowserTest:
  void SetUpCommandLine(base::CommandLine* command_line) override;

  // Returns the popup opened by running `script` in `browser`'s active tab.
  static Browser* OpenPopup(Browser* browser, const std::string& script);

  // Returns the popup opened by running `script` in `adapter`'s frame.
  static Browser* OpenPopup(const content::ToRenderFrameHost& adapter,
                            const std::string& script);

  // Waits for the browser window to move or resize by the given threshold.
  static void WaitForBoundsChange(Browser* browser, int move_by, int resize_by);

  // Grants window-management permission in `browser`'s active tab.
  // Caches a ScreenDetails interface object as `window.screenDetails`.
  // https://www.w3.org/TR/window-management/#screendetails
  static void SetUpWindowManagement(Browser* browser);

  // Returns the display nearest `browser`'s window; see display::Screen.
  static display::Display GetDisplayNearestBrowser(const Browser* browser);

  // Waits for HTML fullscreen of `contents`; returns immediately if already so.
  // https://fullscreen.spec.whatwg.org/
  static void WaitForHTMLFullscreen(content::WebContents* contents);
};

#endif  // CHROME_BROWSER_UI_TEST_POPUP_TEST_BASE_H_

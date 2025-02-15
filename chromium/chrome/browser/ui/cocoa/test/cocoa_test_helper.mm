// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/test/cocoa_test_helper.h"

#include "base/apple/bundle_locations.h"
#include "base/path_service.h"
#include "chrome/common/chrome_constants.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

CocoaTestHelper::CocoaTestHelper() {
  CocoaTest::BootstrapCocoa();
}

CocoaTestHelper::~CocoaTestHelper() = default;

CocoaTest::CocoaTest() {
  BootstrapCocoa();
}

void CocoaTest::BootstrapCocoa() {
  // Look in the framework bundle for resources.
  base::FilePath path;
  base::PathService::Get(base::DIR_EXE, &path);
  path = path.Append(chrome::kFrameworkName);
  base::apple::SetOverrideFrameworkBundlePath(path);
}

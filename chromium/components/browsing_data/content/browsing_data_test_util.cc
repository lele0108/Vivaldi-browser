// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browsing_data/content/browsing_data_test_util.h"

#include <string>
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browsing_data_test_util {

bool HasDataForType(const std::string& type,
                    content::WebContents* web_contents) {
  EXPECT_TRUE(web_contents);
  std::string script = "has" + type + "()";
  return content::EvalJs(web_contents, script).ExtractBool();
}

void SetDataForType(const std::string& type,
                    content::WebContents* web_contents) {
  EXPECT_TRUE(web_contents);
  std::string script = "set" + type + "()";
  ASSERT_TRUE(content::EvalJs(web_contents, script).ExtractBool())
      << "Couldn't create data for: " << type;
}

}  // namespace browsing_data_test_util

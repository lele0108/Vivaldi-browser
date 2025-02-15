// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/storage_access_api/storage_access_api_tab_helper.h"

#include "chrome/browser/storage_access_api/storage_access_api_service.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/test/navigation_simulator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

class MockStorageAccessAPIService : public StorageAccessAPIService {
 public:
  MOCK_METHOD(bool,
              RenewPermissionGrant,
              (const url::Origin& embedded_origin,
               const url::Origin& top_frame_origin),
              (override));
};

class StorageAccessAPITabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    StorageAccessAPITabHelper::CreateForWebContents(web_contents(), &service_);
  }

  content::RenderFrameHost* SimulateNavigateAndCommit(
      const GURL& url,
      content::RenderFrameHost* rfh) {
    std::unique_ptr<content::NavigationSimulator> simulator =
        content::NavigationSimulator::CreateRendererInitiated(url, rfh);
    simulator->Commit();
    return simulator->GetLastThrottleCheckResult().action() ==
                   content::NavigationThrottle::PROCEED
               ? simulator->GetFinalRenderFrameHost()
               : nullptr;
  }

  StorageAccessAPITabHelper* tab_helper() {
    return StorageAccessAPITabHelper::FromWebContents(web_contents());
  }

  MockStorageAccessAPIService& service() { return service_; }

 private:
  testing::StrictMock<MockStorageAccessAPIService> service_;
};

TEST_F(StorageAccessAPITabHelperTest, OnFrameReceivedUserActivation_MainFrame) {
  // The service should not be invoked.
  EXPECT_CALL(service(), RenewPermissionGrant(testing::_, testing::_)).Times(0);

  NavigateAndCommit(GURL("https://example.test/"));

  // Main-frame user activations are no-ops.
  tab_helper()->FrameReceivedUserActivation(main_rfh());
}

TEST_F(StorageAccessAPITabHelperTest, OnFrameReceivedUserActivation_Subframe) {
  EXPECT_CALL(service(), RenewPermissionGrant(
                             url::Origin::Create(GURL("https://bar.test")),
                             url::Origin::Create(GURL("https://example.test"))))
      .Times(1)
      .WillOnce(testing::Return(true));

  NavigateAndCommit(GURL("https://example.test/"));

  content::RenderFrameHost* subframe =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("subframe");

  subframe = SimulateNavigateAndCommit(GURL("https://bar.test/foo"), subframe);
  ASSERT_NE(nullptr, subframe);

  // Non-main-frame user activations cause the service to be invoked.
  tab_helper()->FrameReceivedUserActivation(subframe);
}

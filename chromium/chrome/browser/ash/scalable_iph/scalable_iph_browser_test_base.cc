// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/scalable_iph/scalable_iph_browser_test_base.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "base/functional/bind.h"
#include "chrome/browser/ash/scalable_iph/customizable_test_env_browser_test_base.h"
#include "chrome/browser/ash/scalable_iph/mock_scalable_iph_delegate.h"
#include "chrome/browser/feature_engagement/tracker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/scalable_iph/scalable_iph_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chromeos/ash/components/scalable_iph/scalable_iph.h"
#include "chromeos/ash/components/scalable_iph/scalable_iph_delegate.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace ash {

namespace {
std::set<std::string> mock_delegate_created_;
}

ScalableIphBrowserTestBase::ScalableIphBrowserTestBase() = default;
ScalableIphBrowserTestBase::~ScalableIphBrowserTestBase() = default;

void ScalableIphBrowserTestBase::SetUp() {
  scoped_feature_list_.InitAndEnableFeature(ash::features::kScalableIph);

  // Keyed service is a service which is tied to an object. For our use cases,
  // the object is `BrowserContext` (e.g. `Profile`). See
  // //components/keyed_service/README.md for details on keyed service.
  //
  // We set a testing factory to inject a mock. A testing factory must be set
  // early enough as a service is not created before that, e.g. a `Tracker` must
  // not be created before we set `CreateMockTracker`. If a keyed service is
  // created before we set our testing factory, `SetTestingFactory` will
  // destruct already created keyed services at a time we set our testing
  // factory. It destructs a keyed service at an unusual timing. It can trigger
  // a dangling pointer issue, etc.
  //
  // `SetUpOnMainThread` below is too late to set a testing factory. Note that
  // `InProcessBrowserTest::SetUp` is called at the very early stage, e.g.
  // before command lines are set, etc.
  subscription_ =
      BrowserContextDependencyManager::GetInstance()
          ->RegisterCreateServicesCallbackForTesting(base::BindRepeating(
              &ScalableIphBrowserTestBase::SetTestingFactories));

  CustomizableTestEnvBrowserTestBase::SetUp();
}

// `SetUpOnMainThread` is called just before a test body. Do the mock set up in
// this function as `browser()` is not available in `SetUp` above.
void ScalableIphBrowserTestBase::SetUpOnMainThread() {
  // `CustomizableTestEnvBrowserTestBase::SetUpOnMainThread` must be called
  // before our `SetUpOnMainThread` as login happens in the method, i.e. profile
  // is not available before it.
  CustomizableTestEnvBrowserTestBase::SetUpOnMainThread();
  CHECK(browser()->profile());

  CHECK(IsMockDelegateCreatedFor(browser()->profile()))
      << "ScalableIph service has a timer inside. The service must be created "
         "at a login time. We check the behavior by confirming creation of a "
         "delegate.";

  mock_tracker_ = static_cast<feature_engagement::test::MockTracker*>(
      feature_engagement::TrackerFactory::GetForBrowserContext(
          browser()->profile()));
  CHECK(mock_tracker_)
      << "mock_tracker_ must be non-nullptr. GetForBrowserContext should "
         "create one via CreateMockTracker if it does not exist.";

  ON_CALL(*mock_tracker_, AddOnInitializedCallback)
      .WillByDefault(
          [](feature_engagement::Tracker::OnInitializedCallback callback) {
            std::move(callback).Run(true);
          });

  ON_CALL(*mock_tracker_, IsInitialized).WillByDefault(testing::Return(true));

  CHECK(ScalableIphFactory::GetInstance()->has_delegate_factory_for_testing())
      << "This test uses MockScalableIphDelegate. A factory for testing must "
         "be set.";
  scalable_iph::ScalableIph* scalable_iph =
      ScalableIphFactory::GetForBrowserContext(browser()->profile());
  CHECK(scalable_iph);

  // `ScalableIph` for the profile is initialzied in
  // `CustomizableTestEnvBrowserTestBase::SetUpOnMainThread` above. We cannot
  // simply use `TestMockTimeTaskRunner::ScopedContext` as `RunLoop` is used
  // there and it's not supported by `ScopedContext`. We override a task runner
  // after a timer has created and started.
  task_runner_ = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  scalable_iph->OverrideTaskRunnerForTesting(task_runner());

  mock_delegate_ = static_cast<test::MockScalableIphDelegate*>(
      scalable_iph->delegate_for_testing());
  CHECK(mock_delegate_);
}

void ScalableIphBrowserTestBase::TearDownOnMainThread() {
  // We are going to release references to mock objects below. Verify the
  // expectations in advance to have a predictable behavior.
  testing::Mock::VerifyAndClearExpectations(mock_tracker_);
  mock_tracker_ = nullptr;
  testing::Mock::VerifyAndClearExpectations(mock_delegate_);
  mock_delegate_ = nullptr;

  InProcessBrowserTest::TearDownOnMainThread();
}

bool ScalableIphBrowserTestBase::IsMockDelegateCreatedFor(Profile* profile) {
  return mock_delegate_created_.contains(profile->GetProfileUserName());
}

void ScalableIphBrowserTestBase::ShutdownScalableIph() {
  scalable_iph::ScalableIph* scalable_iph =
      ScalableIphFactory::GetForBrowserContext(browser()->profile());
  CHECK(scalable_iph) << "ScalableIph does not exist for a current profile";

  // `ScalableIph::Shutdown` destructs a delegate. Release the pointer to the
  // mock delegate to avoid having a dangling pointer. We can retain a pointer
  // to the mock tracker as a tracker is not destructed by the
  // `ScalableIph::Shutdown`.
  mock_delegate_ = nullptr;

  scalable_iph->Shutdown();
}

// static
void ScalableIphBrowserTestBase::SetTestingFactories(
    content::BrowserContext* browser_context) {
  feature_engagement::TrackerFactory::GetInstance()->SetTestingFactory(
      browser_context,
      base::BindRepeating(&ScalableIphBrowserTestBase::CreateMockTracker));

  ScalableIphFactory* scalable_iph_factory = ScalableIphFactory::GetInstance();
  CHECK(scalable_iph_factory);

  // This method can be called more than once for a single browser context.
  if (scalable_iph_factory->has_delegate_factory_for_testing()) {
    return;
  }

  // This is NOT a testing factory of a keyed service factory .But the delegate
  // factory is called from the factory of `ScalableIphFactory`. Set this at the
  // same time.
  scalable_iph_factory->SetDelegateFactoryForTesting(
      base::BindRepeating(&ScalableIphBrowserTestBase::CreateMockDelegate));
}

// static
std::unique_ptr<KeyedService> ScalableIphBrowserTestBase::CreateMockTracker(
    content::BrowserContext* browser_context) {
  return std::make_unique<feature_engagement::test::MockTracker>();
}

// static
std::unique_ptr<scalable_iph::ScalableIphDelegate>
ScalableIphBrowserTestBase::CreateMockDelegate(Profile* profile) {
  std::pair<std::set<std::string>::iterator, bool> result =
      mock_delegate_created_.insert(profile->GetProfileUserName());
  CHECK(result.second) << "Delegate is created twice for a profile";

  return std::make_unique<test::MockScalableIphDelegate>();
}

}  // namespace ash

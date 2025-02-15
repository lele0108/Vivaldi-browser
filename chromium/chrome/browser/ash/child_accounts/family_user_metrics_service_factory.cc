// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/child_accounts/family_user_metrics_service_factory.h"

#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/child_accounts/child_user_service_factory.h"
#include "chrome/browser/ash/child_accounts/family_user_metrics_service.h"
#include "chrome/browser/supervised_user/supervised_user_browser_utils.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "components/supervised_user/core/common/features.h"
#include "content/public/browser/browser_context.h"

namespace ash {

// static
FamilyUserMetricsService* FamilyUserMetricsServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<FamilyUserMetricsService*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

// static
FamilyUserMetricsServiceFactory*
FamilyUserMetricsServiceFactory::GetInstance() {
  static base::NoDestructor<FamilyUserMetricsServiceFactory> factory;
  return factory.get();
}

FamilyUserMetricsServiceFactory::FamilyUserMetricsServiceFactory()
    : ProfileKeyedServiceFactory(
          "FamilyUserMetricsServiceFactory",
          base::FeatureList::IsEnabled(
              supervised_user::kUpdateSupervisedUserFactoryCreation)
              ? ProfileSelections::BuildForRegularProfile()
              : supervised_user::BuildProfileSelectionsLegacy()) {
  DependsOn(apps::AppServiceProxyFactory::GetInstance());
  DependsOn(ChildUserServiceFactory::GetInstance());
  DependsOn(SupervisedUserServiceFactory::GetInstance());
}

FamilyUserMetricsServiceFactory::~FamilyUserMetricsServiceFactory() = default;

KeyedService* FamilyUserMetricsServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new FamilyUserMetricsService(context);
}

}  // namespace ash

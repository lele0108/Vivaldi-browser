// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREDICTORS_LCP_CRITICAL_PATH_PREDICTOR_LCP_CRITICAL_PATH_PREDICTOR_KEYED_SERVICE_FACTORY_H_
#define CHROME_BROWSER_PREDICTORS_LCP_CRITICAL_PATH_PREDICTOR_LCP_CRITICAL_PATH_PREDICTOR_KEYED_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;
class LCPCriticalPathPredictorKeyedService;

// Singleton that creates the LCPCriticalPathPredictorKeyedServices and
// associates them with Profiles.
class LCPCriticalPathPredictorKeyedServiceFactory
    : public ProfileKeyedServiceFactory {
 public:
  static LCPCriticalPathPredictorKeyedService* GetForProfile(Profile* profile);

  LCPCriticalPathPredictorKeyedServiceFactory(
      const LCPCriticalPathPredictorKeyedServiceFactory&) = delete;
  LCPCriticalPathPredictorKeyedServiceFactory& operator=(
      const LCPCriticalPathPredictorKeyedServiceFactory&) = delete;

 private:
  friend class base::NoDestructor<LCPCriticalPathPredictorKeyedServiceFactory>;
  static LCPCriticalPathPredictorKeyedServiceFactory& GetInstance();

  LCPCriticalPathPredictorKeyedServiceFactory();
  ~LCPCriticalPathPredictorKeyedServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

#endif  // CHROME_BROWSER_PREDICTORS_LCP_CRITICAL_PATH_PREDICTOR_LCP_CRITICAL_PATH_PREDICTOR_KEYED_SERVICE_FACTORY_H_

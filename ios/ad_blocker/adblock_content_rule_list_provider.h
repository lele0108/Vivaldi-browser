// Copyright (c) 2023 Vivaldi Technologies AS. All rights reserved

#ifndef IOS_AD_BLOCKER_ADBLOCK_CONTENT_RULE_LIST_PROVIDER_H_
#define IOS_AD_BLOCKER_ADBLOCK_CONTENT_RULE_LIST_PROVIDER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/values.h"
#include "components/ad_blocker/adblock_metadata.h"

namespace web {
class BrowserState;
}

namespace adblock_filter {

// A provider class that handles compiling and configuring Content Blocker
// rules.
class AdBlockerContentRuleListProvider {
 public:
  static std::unique_ptr<AdBlockerContentRuleListProvider> Create(
      web::BrowserState* browser_state,
      RuleGroup group,
      base::OnceClosure on_loaded,
      base::RepeatingClosure on_rules_applied);
  virtual ~AdBlockerContentRuleListProvider();

  virtual void InstallContentRuleLists(const base::Value::List& lists) = 0;
  virtual void ApplyLoadedRules() = 0;
};

}  // namespace adblock_filter

#endif  // IOS_AD_BLOCKER_ADBLOCK_CONTENT_RULE_LIST_PROVIDER_H_

// Copyright (c) 2023 Vivaldi Technologies AS. All rights reserved

#ifndef IOS_AD_BLOCKER_ADBLOCK_ORGANIZED_RULES_MANAGER_H_
#define IOS_AD_BLOCKER_ADBLOCK_ORGANIZED_RULES_MANAGER_H_

#include <map>

#include "base/cancelable_callback.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/ad_blocker/adblock_metadata.h"
#include "components/ad_blocker/adblock_rule_manager.h"
#include "components/ad_blocker/adblock_rule_service.h"
#include "ios/ad_blocker/adblock_rules_organizer.h"

namespace adblock_filter {
class RuleService;
class CompiledRules;
class AdBlockerContentRuleListProvider;

class OrganizedRulesManager : public RuleManager::Observer {
  using OrganizedRulesChangedCallback =
      base::RepeatingCallback<void(RuleService::IndexBuildResult build_result)>;
  using RulesReadFailCallback =
      base::RepeatingCallback<void(RuleGroup rule_group, uint32_t source_id)>;

 public:
  OrganizedRulesManager(
      RuleService* rule_service,
      std::unique_ptr<AdBlockerContentRuleListProvider>
          content_rule_list_provider,
      RuleGroup group,
      base::FilePath browser_state_path,
      const std::string& organized_rules_checksum,
      OrganizedRulesChangedCallback organized_rules_changed_callback,
      RulesReadFailCallback rule_read_fail_callback,
      scoped_refptr<base::SequencedTaskRunner> file_task_runner);
  ~OrganizedRulesManager() override;
  OrganizedRulesManager(const OrganizedRulesManager&) = delete;
  OrganizedRulesManager& operator=(const OrganizedRulesManager&) = delete;

  RuleGroup group() const { return group_; }
  std::string organized_rules_checksum() const {
    return organized_rules_checksum_;
  }

  RuleService::IndexBuildResult build_result() const { return build_result_; }

 private:
  void OnRulesSourceUpdated(const RuleSource& rule_source) override;
  void OnRuleSourceDeleted(uint32_t source_id, RuleGroup group) override;
  void OnExceptionListStateChanged(RuleGroup group) override;
  void OnExceptionListChanged(RuleGroup group,
                              RuleManager::ExceptionsList list) override;

  void ReadCompiledRules(const RuleSource& rule_source);
  void OnRulesRead(uint32_t source_id,
                   const std::string& checksum,
                   std::unique_ptr<base::Value> compiled_rules);

  void UpdateExceptions();
  void ReorganizeRules();

  void OnOrganizedRulesLoaded(std::string checksum,
                              std::unique_ptr<base::Value> rules);
  void OnOrganizedRulesReady(base::Value rules);
  void Disable();
  void ApplyOrganizedRules(base::Value rules, bool save);

  const raw_ptr<RuleManager> rule_manager_;
  std::unique_ptr<AdBlockerContentRuleListProvider> content_rule_list_provider_;
  RuleGroup group_;

  bool is_loaded_;
  RuleService::IndexBuildResult build_result_ = RuleService::kBuildSuccess;

  std::map<uint32_t, RuleSource> rule_sources_;
  base::FilePath rules_list_folder_;

  base::CancelableOnceCallback<void(base::Value)>
      organized_rules_ready_callback_;

  std::map<uint32_t, scoped_refptr<CompiledRules>> compiled_rules_;
  base::Value exception_rule_;

  std::string organized_rules_checksum_;

  OrganizedRulesChangedCallback organized_rules_changed_callback_;
  RulesReadFailCallback rule_read_fail_callback_;

  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;

  base::WeakPtrFactory<OrganizedRulesManager> weak_factory_{this};
};
}  // namespace adblock_filter

#endif  // IOS_AD_BLOCKER_ADBLOCK_ORGANIZED_RULES_MANAGER_H_

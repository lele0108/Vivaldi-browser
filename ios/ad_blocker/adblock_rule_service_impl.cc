// Copyright (c) 2023 Vivaldi Technologies AS. All rights reserved

#include "ios/ad_blocker/adblock_rule_service_impl.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "components/ad_blocker/adblock_known_sources_handler.h"
#include "components/ad_blocker/adblock_rule_manager_impl.h"
#include "components/ad_blocker/adblock_rule_source_handler.h"
#include "ios/ad_blocker/adblock_content_rule_list_provider.h"
#include "ios/web/public/browser_state.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace adblock_filter {

struct RuleServiceImpl::LoadData {
  std::array<std::unique_ptr<AdBlockerContentRuleListProvider>, kRuleGroupCount>
      loading_content_rule_list_providers;
  RuleServiceStorage::LoadResult load_result;
};

RuleServiceImpl::RuleServiceImpl(
    web::BrowserState* browser_state,
    RuleSourceHandler::RulesCompiler rules_compiler,
    std::string locale)
    : browser_state_(browser_state),
      rules_compiler_(std::move(rules_compiler)),
      locale_(std::move(locale)) {}

RuleServiceImpl::~RuleServiceImpl() = default;

void RuleServiceImpl::AddObserver(RuleService::Observer* observer) {
  observers_.AddObserver(observer);
}

void RuleServiceImpl::RemoveObserver(RuleService::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void RuleServiceImpl::Load() {
  DCHECK(!is_loaded_ && !state_store_);
  file_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  state_store_.emplace(browser_state_->GetStatePath(), this, file_task_runner_);

  auto load_data = std::make_unique<LoadData>();
  LoadData* load_data_ptr = load_data.get();

  auto on_loading_done = base::BarrierClosure(
      kRuleGroupCount + 1,
      base::BindOnce(&RuleServiceImpl::OnStateLoaded,
                     weak_ptr_factory_.GetWeakPtr(), std::move(load_data)));

  for (auto group : {RuleGroup::kTrackingRules, RuleGroup::kAdBlockingRules}) {
    load_data_ptr
        ->loading_content_rule_list_providers[static_cast<size_t>(group)] =
        AdBlockerContentRuleListProvider::Create(
            browser_state_, group, on_loading_done,
            base::BindRepeating(&RuleServiceImpl::OnRulesApplied,
                                weak_ptr_factory_.GetWeakPtr(), group));
  }

  state_store_->Load(base::BindOnce(
      [](base::RepeatingClosure on_loading_done, LoadData* load_data,
         RuleServiceStorage::LoadResult load_result) {
        load_data->load_result = std::move(load_result);
        on_loading_done.Run();
      },
      on_loading_done, load_data_ptr));
}

bool RuleServiceImpl::IsLoaded() const {
  return is_loaded_;
}

void RuleServiceImpl::Shutdown() {
  if (is_loaded_) {
    state_store_->OnRuleServiceShutdown();
    rule_manager_->RemoveObserver(this);
  }
}

bool RuleServiceImpl::IsRuleGroupEnabled(RuleGroup group) const {
  return true;
}

void RuleServiceImpl::SetRuleGroupEnabled(RuleGroup group, bool enabled) {
  DCHECK(is_loaded_);
  if (IsRuleGroupEnabled(group) == enabled)
    return;

  NOTREACHED();
  // TODO(juliem): Implement

  for (RuleService::Observer& observer : observers_)
    observer.OnGroupStateChanged(group);

  state_store_->ScheduleSave();
}

void RuleServiceImpl::OnStateLoaded(std::unique_ptr<LoadData> load_data) {
  RuleServiceStorage::LoadResult& load_result = load_data->load_result;
  // All cases of base::Unretained here are safe. We are generally passing
  // callbacks to objects that we own, calling to either this or other objects
  // that we own.

  rule_manager_.emplace(
      file_task_runner_, browser_state_->GetStatePath(),
      browser_state_->GetSharedURLLoaderFactory(),
      std::move(load_result.rule_sources),
      std::move(load_result.active_exceptions_lists),
      std::move(load_result.exceptions),
      base::BindRepeating(&RuleServiceStorage::ScheduleSave,
                          base::Unretained(&state_store_.value())),
      rules_compiler_, base::DoNothing());
  rule_manager_->AddObserver(this);

  known_sources_handler_.emplace(
      this, load_result.storage_version, locale_, load_result.known_sources,
      std::move(load_result.deleted_presets),
      base::BindRepeating(&RuleServiceStorage::ScheduleSave,
                          base::Unretained(&state_store_.value())));

  for (auto group : {RuleGroup::kTrackingRules, RuleGroup::kAdBlockingRules}) {
    organized_rules_manager_[static_cast<size_t>(group)].emplace(
        this,
        std::move(
            load_data->loading_content_rule_list_providers[static_cast<size_t>(
                group)]),
        group, browser_state_->GetStatePath(),
        load_result.index_checksums[static_cast<size_t>(group)],
        base::BindRepeating(&RuleServiceImpl::OnRulesIndexChanged,
                            base::Unretained(this), group),
        base::BindRepeating(&RuleManager::OnCompiledRulesReadFailCallback,
                            base::Unretained(&rule_manager_.value())),
        file_task_runner_);
  }

  is_loaded_ = true;
  for (RuleService::Observer& observer : observers_)
    observer.OnRuleServiceStateLoaded(this);
}

std::string RuleServiceImpl::GetRulesIndexChecksum(RuleGroup group) {
  DCHECK(organized_rules_manager_[static_cast<size_t>(group)]);
  return organized_rules_manager_[static_cast<size_t>(group)]
      ->organized_rules_checksum();
}

RuleService::IndexBuildResult RuleServiceImpl::GetRulesIndexBuildResult(
    RuleGroup group) {
  DCHECK(organized_rules_manager_[static_cast<size_t>(group)]);
  return organized_rules_manager_[static_cast<size_t>(group)]->build_result();
}

RuleManager* RuleServiceImpl::GetRuleManager() {
  DCHECK(rule_manager_);
  return &rule_manager_.value();
}

KnownRuleSourcesHandler* RuleServiceImpl::GetKnownSourcesHandler() {
  DCHECK(known_sources_handler_);
  return &known_sources_handler_.value();
}

BlockedUrlsReporter* RuleServiceImpl::GetBlockerUrlsReporter() {
  return nullptr;
}

void RuleServiceImpl::OnExceptionListChanged(RuleGroup group,
                                             RuleManager::ExceptionsList list) {
}

void RuleServiceImpl::OnRulesIndexChanged(
    RuleGroup group,
    RuleService::IndexBuildResult build_result) {
  // The state store will read all checksums when saving. No need to worry about
  // which has changed.
  state_store_->ScheduleSave();
  for (RuleService::Observer& observer : observers_)
    observer.OnRulesIndexBuilt(group, build_result);
}

void RuleServiceImpl::OnRulesApplied(RuleGroup group) {
  for (RuleService::Observer& observer : observers_)
    observer.OnIosRulesApplied(group);
}

}  // namespace adblock_filter

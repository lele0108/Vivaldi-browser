// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_
#define CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_

#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/updater/app/app.h"
#include "chrome/updater/mac/privileged_helper/service.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace updater {

class PrivilegedHelperServer : public App {
 public:
  PrivilegedHelperServer();
  void TaskStarted();
  void TaskCompleted();

 protected:
  ~PrivilegedHelperServer() override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // Overrides for App.
  void Initialize() override;
  void FirstTaskRun() override;
  void Uninitialize() override;

  void Uninstall();
  void MarkTaskStarted();
  void AcknowledgeTaskCompletion();
  base::TimeDelta ServerKeepAlive();

  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  scoped_refptr<PrivilegedHelperService> service_;
  NSXPCListener* __strong service_listener_;
  PrivilegedHelperServiceXPCDelegate* __strong service_delegate_;
  int tasks_running_ = 0;
};

scoped_refptr<App> PrivilegedHelperServerInstance();

}  // namespace updater

#endif  // CHROME_UPDATER_MAC_PRIVILEGED_HELPER_SERVER_H_

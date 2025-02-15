// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/it2me/it2me_native_messaging_host_chromeos.h"

#include <memory>
#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "remoting/host/chromeos/browser_interop.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/it2me/it2me_native_messaging_host.h"
#include "remoting/host/policy_watcher.h"

namespace remoting {

std::unique_ptr<extensions::NativeMessageHost>
CreateIt2MeNativeMessagingHostForChromeOS() {
  return std::make_unique<It2MeNativeMessagingHost>(
      /*needs_elevation=*/false, CreatePolicyWatcher(),
      CreateChromotingHostContext(), std::make_unique<It2MeHostFactory>());
}

}  // namespace remoting

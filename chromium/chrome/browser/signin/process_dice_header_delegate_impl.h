// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_PROCESS_DICE_HEADER_DELEGATE_IMPL_H_
#define CHROME_BROWSER_SIGNIN_PROCESS_DICE_HEADER_DELEGATE_IMPL_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/signin/dice_response_handler.h"
#include "components/signin/public/base/signin_metrics.h"

namespace content {
class WebContents;
}

class Profile;
class SigninUIError;

class ProcessDiceHeaderDelegateImpl : public ProcessDiceHeaderDelegate {
 public:
  // Callback starting Sync.
  using EnableSyncCallback =
      base::OnceCallback<void(Profile*,
                              signin_metrics::AccessPoint,
                              signin_metrics::PromoAction,
                              signin_metrics::Reason,
                              content::WebContents*,
                              const CoreAccountId&)>;

  // Callback showing a signin error UI.
  using ShowSigninErrorCallback = base::OnceCallback<
      void(Profile*, content::WebContents*, const SigninUIError&)>;

  // Helper function for creating `ProcessDiceHeaderDelegateImpl` from a
  // `content::WebContents`.
  static std::unique_ptr<ProcessDiceHeaderDelegateImpl> Create(
      content::WebContents* web_contents,
      EnableSyncCallback enable_sync_callback,
      ShowSigninErrorCallback show_signin_error_callback);

  // |is_sync_signin_tab| is true if a sync signin flow has been started in that
  // tab.
  ProcessDiceHeaderDelegateImpl(
      content::WebContents* web_contents,
      bool is_sync_signin_tab,
      signin_metrics::AccessPoint access_point,
      signin_metrics::PromoAction promo_action,
      signin_metrics::Reason reason,
      GURL redirect_url,
      EnableSyncCallback enable_sync_callback,
      ShowSigninErrorCallback show_signin_error_callback);

  ProcessDiceHeaderDelegateImpl(const ProcessDiceHeaderDelegateImpl&) = delete;
  ProcessDiceHeaderDelegateImpl& operator=(
      const ProcessDiceHeaderDelegateImpl&) = delete;

  ~ProcessDiceHeaderDelegateImpl() override;

  // ProcessDiceHeaderDelegate:
  void HandleTokenExchangeSuccess(CoreAccountId account_id,
                                  bool is_new_account) override;
  void EnableSync(const CoreAccountId& account_id) override;
  void HandleTokenExchangeFailure(const std::string& email,
                                  const GoogleServiceAuthError& error) override;
  signin_metrics::AccessPoint GetAccessPoint() override;

 private:
  // Returns true if sync should be enabled after the user signs in.
  bool ShouldEnableSync();

  const base::WeakPtr<content::WebContents> web_contents_;
  const raw_ref<Profile> profile_;
  const bool is_sync_signin_tab_;
  const signin_metrics::AccessPoint access_point_;
  const signin_metrics::PromoAction promo_action_;
  const signin_metrics::Reason reason_;
  const GURL redirect_url_;
  EnableSyncCallback enable_sync_callback_;
  ShowSigninErrorCallback show_signin_error_callback_;
};

#endif  // CHROME_BROWSER_SIGNIN_PROCESS_DICE_HEADER_DELEGATE_IMPL_H_

// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/process_dice_header_delegate_impl.h"

#include <utility>

#include "base/functional/callback.h"
#include "base/logging.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/dice_tab_helper.h"
#include "chrome/browser/signin/dice_web_signin_interceptor.h"
#include "chrome/browser/signin/dice_web_signin_interceptor_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/webui/signin/signin_ui_error.h"
#include "chrome/common/url_constants.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace {

void RedirectToNtp(content::WebContents* contents) {
  VLOG(1) << "RedirectToNtp";
  contents->GetController().LoadURL(
      GURL(chrome::kChromeUINewTabURL), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
}

// Helper function similar to DiceTabHelper::FromWebContents(), but also handles
// the case where |contents| is nullptr.
DiceTabHelper* GetDiceTabHelperFromWebContents(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  return DiceTabHelper::FromWebContents(contents);
}

}  // namespace

// static
std::unique_ptr<ProcessDiceHeaderDelegateImpl>
ProcessDiceHeaderDelegateImpl::Create(
    content::WebContents* web_contents,
    EnableSyncCallback enable_sync_callback,
    ShowSigninErrorCallback show_signin_error_callback) {
  bool is_sync_signin_tab = false;
  signin_metrics::AccessPoint access_point =
      signin_metrics::AccessPoint::ACCESS_POINT_UNKNOWN;
  signin_metrics::PromoAction promo_action =
      signin_metrics::PromoAction::PROMO_ACTION_NO_SIGNIN_PROMO;
  signin_metrics::Reason reason = signin_metrics::Reason::kUnknownReason;
  GURL redirect_url;

  DiceTabHelper* tab_helper = DiceTabHelper::FromWebContents(web_contents);
  if (tab_helper) {
    is_sync_signin_tab = tab_helper->IsSyncSigninInProgress();
    redirect_url = tab_helper->redirect_url();
    access_point = tab_helper->signin_access_point();
    promo_action = tab_helper->signin_promo_action();
    reason = tab_helper->signin_reason();
  } else {
    access_point = signin_metrics::AccessPoint::ACCESS_POINT_WEB_SIGNIN;
  }
  return std::make_unique<ProcessDiceHeaderDelegateImpl>(
      web_contents, is_sync_signin_tab, access_point, promo_action, reason,
      std::move(redirect_url), std::move(enable_sync_callback),
      std::move(show_signin_error_callback));
}

ProcessDiceHeaderDelegateImpl::ProcessDiceHeaderDelegateImpl(
    content::WebContents* web_contents,
    bool is_sync_signin_tab,
    signin_metrics::AccessPoint access_point,
    signin_metrics::PromoAction promo_action,
    signin_metrics::Reason reason,
    GURL redirect_url,
    EnableSyncCallback enable_sync_callback,
    ShowSigninErrorCallback show_signin_error_callback)
    : web_contents_(web_contents->GetWeakPtr()),
      profile_(raw_ref<Profile>::from_ptr(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()))),
      is_sync_signin_tab_(is_sync_signin_tab),
      access_point_(access_point),
      promo_action_(promo_action),
      reason_(reason),
      redirect_url_(std::move(redirect_url)),
      enable_sync_callback_(std::move(enable_sync_callback)),
      show_signin_error_callback_(std::move(show_signin_error_callback)) {}

ProcessDiceHeaderDelegateImpl::~ProcessDiceHeaderDelegateImpl() = default;

bool ProcessDiceHeaderDelegateImpl::ShouldEnableSync() {
  if (IdentityManagerFactory::GetForProfile(&profile_.get())
          ->HasPrimaryAccount(signin::ConsentLevel::kSync)) {
    VLOG(1) << "Do not start sync after web sign-in [already authenticated].";
    return false;
  }

  if (!is_sync_signin_tab_) {
    VLOG(1)
        << "Do not start sync after web sign-in [not a Chrome sign-in tab].";
    return false;
  }

  return true;
}

void ProcessDiceHeaderDelegateImpl::HandleTokenExchangeSuccess(
    CoreAccountId account_id,
    bool is_new_account) {
  // is_sync_signin_tab_ tells whether the current signin is happening in a tab
  // that was opened from a "Enable Sync" Chrome UI. Usually this is indeed a
  // sync signin, but it is not always the case: the user may abandon the sync
  // signin and do a simple web signin in the same tab instead.
  DiceWebSigninInterceptorFactory::GetForProfile(&profile_.get())
      ->MaybeInterceptWebSignin(web_contents_.get(), account_id, is_new_account,
                                is_sync_signin_tab_);
}

void ProcessDiceHeaderDelegateImpl::EnableSync(
    const CoreAccountId& account_id) {
  DiceTabHelper* tab_helper =
      GetDiceTabHelperFromWebContents(web_contents_.get());
  if (tab_helper) {
    tab_helper->OnSyncSigninFlowComplete();
  }

  if (!ShouldEnableSync()) {
    // No special treatment is needed if the user is not enabling sync.
    return;
  }

  content::WebContents* web_contents = web_contents_.get();
  VLOG(1) << "Start sync after web sign-in.";
  std::move(enable_sync_callback_)
      .Run(&profile_.get(), access_point_, promo_action_, reason_, web_contents,
           account_id);

  if (!web_contents) {
    return;
  }

  // After signing in to Chrome, the user should be redirected to the NTP,
  // unless specified otherwise.
  if (redirect_url_.is_empty()) {
    RedirectToNtp(web_contents);
    return;
  }

  DCHECK(redirect_url_.is_valid());
  web_contents->GetController().LoadURL(redirect_url_, content::Referrer(),
                                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                        std::string());
}

void ProcessDiceHeaderDelegateImpl::HandleTokenExchangeFailure(
    const std::string& email,
    const GoogleServiceAuthError& error) {
  DCHECK_NE(GoogleServiceAuthError::NONE, error.state());
  DiceTabHelper* tab_helper =
      GetDiceTabHelperFromWebContents(web_contents_.get());
  if (tab_helper) {
    tab_helper->OnSyncSigninFlowComplete();
  }

  bool should_enable_sync = ShouldEnableSync();

  content::WebContents* web_contents = web_contents_.get();
  if (should_enable_sync && web_contents) {
    RedirectToNtp(web_contents);
  }

  // Show the error even if the WebContents was closed, because the user may be
  // signed out of the web.
  std::move(show_signin_error_callback_)
      .Run(&profile_.get(), web_contents,
           SigninUIError::FromGoogleServiceAuthError(email, error));
}

signin_metrics::AccessPoint ProcessDiceHeaderDelegateImpl::GetAccessPoint() {
  return access_point_;
}

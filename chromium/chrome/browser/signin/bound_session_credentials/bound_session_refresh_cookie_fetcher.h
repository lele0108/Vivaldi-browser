// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_BOUND_SESSION_CREDENTIALS_BOUND_SESSION_REFRESH_COOKIE_FETCHER_H_
#define CHROME_BROWSER_SIGNIN_BOUND_SESSION_CREDENTIALS_BOUND_SESSION_REFRESH_COOKIE_FETCHER_H_

#include <ostream>

#include "base/functional/callback_forward.h"
#include "net/base/net_errors.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

// This class makes the network request to the Gaia cookie rotation endpoint to
// refresh bound Google authentication cookies. A new fetcher instance should be
// created per request.
class BoundSessionRefreshCookieFetcher {
 public:
  enum class Result {
    kSuccess = 0,
    kConnectionError = 1,
    kServerTransientError = 2,
    kServerPersistentError = 3,
  };

  // Reports the result of the fetch request.
  using RefreshCookieCompleteCallback = base::OnceCallback<void(Result)>;

  BoundSessionRefreshCookieFetcher() = default;
  virtual ~BoundSessionRefreshCookieFetcher() = default;

  BoundSessionRefreshCookieFetcher(const BoundSessionRefreshCookieFetcher&) =
      delete;
  BoundSessionRefreshCookieFetcher& operator=(
      const BoundSessionRefreshCookieFetcher&) = delete;

  // Starts the network request to the Gaia rotation endpoint. `callback` is
  // called with the fetch results upon completion. Should be called no more
  // than once per instance.
  virtual void Start(RefreshCookieCompleteCallback callback) = 0;
};

std::ostream& operator<<(
    std::ostream& os,
    const BoundSessionRefreshCookieFetcher::Result& result);

#endif  // CHROME_BROWSER_SIGNIN_BOUND_SESSION_CREDENTIALS_BOUND_SESSION_REFRESH_COOKIE_FETCHER_H_

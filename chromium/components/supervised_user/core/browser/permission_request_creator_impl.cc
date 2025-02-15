// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/supervised_user/core/browser/permission_request_creator_impl.h"
#include <memory>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "components/supervised_user/core/browser/proto/kidschromemanagement_messages.pb.h"
#include "components/supervised_user/core/browser/proto_fetcher.h"

namespace supervised_user {

namespace {
void DispatchResult(bool success,
                    PermissionRequestCreator::SuccessCallback callback) {
  std::move(callback).Run(success);
}

// TODO(b/4598236): Remove the validation and handle invalid responses
// in proto_fetcher (using a DataError status).
bool ValidateResponse(
    const kids_chrome_management::CreatePermissionRequestResponse& response) {
  if (!response.has_permission_request()) {
    DLOG(WARNING) << "Permission request not found";
    return false;
  }
  if (!response.permission_request().has_id()) {
    DLOG(WARNING) << "ID not found";
    return false;
  }
  return true;
}

void OnSuccess(
    const kids_chrome_management::CreatePermissionRequestResponse& response,
    PermissionRequestCreator::SuccessCallback callback) {
  DispatchResult(ValidateResponse(response), std::move(callback));
}

void OnFailure(ProtoFetcherStatus error,
               PermissionRequestCreator::SuccessCallback callback) {
  DispatchResult(false, std::move(callback));
}

void OnResponse(
    std::unique_ptr<DeferredProtoFetcher<
        kids_chrome_management::CreatePermissionRequestResponse>> fetcher,
    PermissionRequestCreator::SuccessCallback callback,
    ProtoFetcherStatus status,
    std::unique_ptr<kids_chrome_management::CreatePermissionRequestResponse>
        response) {
  if (!status.IsOk()) {
    OnFailure(status, std::move(callback));
    return;
  }
  OnSuccess(*response, std::move(callback));
}
}  // namespace

PermissionRequestCreatorImpl::PermissionRequestCreatorImpl(
    signin::IdentityManager* identity_manager,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : identity_manager_(identity_manager),
      url_loader_factory_(url_loader_factory) {
  CHECK(identity_manager);
}

PermissionRequestCreatorImpl::~PermissionRequestCreatorImpl() = default;

bool PermissionRequestCreatorImpl::IsEnabled() const {
  return true;
}

void PermissionRequestCreatorImpl::CreateURLAccessRequest(
    const GURL& url_requested,
    SuccessCallback callback) {
  kids_chrome_management::PermissionRequest request =
      kids_chrome_management::PermissionRequest();
  request.mutable_object_ref()->assign(url_requested.spec());
  request.set_state(kids_chrome_management::PermissionRequestState::PENDING);
  request.set_event_type(
      kids_chrome_management::FamilyEventType::PERMISSION_CHROME_URL);

  std::unique_ptr<DeferredProtoFetcher<
      kids_chrome_management::CreatePermissionRequestResponse>>
      fetcher = CreatePermissionRequestFetcher(*identity_manager_,
                                               url_loader_factory_, request);

  auto* fetcher_ptr = fetcher.get();
  fetcher_ptr->Start(
      base::BindOnce(&OnResponse,
                     // The fetcher is bound to ensure its lifetime, but is not
                     // used in the response processing.
                     std::move(fetcher), std::move(callback)));
}

}  // namespace supervised_user

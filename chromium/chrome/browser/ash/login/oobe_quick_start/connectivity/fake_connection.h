// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_CONNECTIVITY_FAKE_CONNECTION_H_
#define CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_CONNECTIVITY_FAKE_CONNECTION_H_

#include <cstdint>
#include "chrome/browser/ash/login/oobe_quick_start/connectivity/connection.h"
#include "chrome/browser/ash/login/oobe_quick_start/connectivity/fido_assertion_info.h"
#include "chromeos/ash/services/nearby/public/mojom/quick_start_decoder_types.mojom.h"

namespace ash::quick_start {

class FakeConnection : public Connection {
 public:
  class Factory : public Connection::Factory {
   public:
    Factory();
    ~Factory() override;

    // Connection::Factory:
    std::unique_ptr<Connection> Create(
        NearbyConnection* nearby_connection,
        Connection::SessionContext session_context,
        mojo::SharedRemote<mojom::QuickStartDecoder> quick_start_decoder,
        ConnectionClosedCallback on_connection_closed,
        ConnectionAuthenticatedCallback on_connection_authenticated) override;

    base::WeakPtr<FakeConnection> instance_;
  };

  FakeConnection(
      NearbyConnection* nearby_connection,
      Connection::SessionContext session_context,
      mojo::SharedRemote<mojom::QuickStartDecoder> quick_start_decoder,
      ConnectionClosedCallback on_connection_closed,
      ConnectionAuthenticatedCallback on_connection_authenticated);

  ~FakeConnection() override;

  // Connection:
  void InitiateHandshake(const std::string& authentication_token,
                         HandshakeSuccessCallback callback) override;
  void RequestWifiCredentials(int32_t session_id,
                              RequestWifiCredentialsCallback callback) override;
  void WaitForUserVerification(AwaitUserVerificationCallback callback) override;
  void RequestAccountTransferAssertion(
      const std::string& challenge_b64url,
      RequestAccountTransferAssertionCallback callback) override;

  bool WasHandshakeInitiated();
  void SendWifiCredentials(absl::optional<mojom::WifiCredentials> credentials);
  void VerifyUser(absl::optional<mojom::UserVerificationResponse> response);
  void SendAccountTransferAssertionInfo(
      absl::optional<FidoAssertionInfo> assertion_info);
  void HandleHandshakeResult(bool success);

  void set_phone_instance_id(std::string phone_instance_id) {
    phone_instance_id_ = phone_instance_id;
  }

 private:
  bool handshake_initiated_ = false;
  HandshakeSuccessCallback handshake_success_callback_;
  RequestWifiCredentialsCallback wifi_credentials_callback_;
  AwaitUserVerificationCallback await_user_verification_callback_;
  RequestAccountTransferAssertionCallback
      request_account_transfer_assertion_callback_;

  base::WeakPtrFactory<FakeConnection> weak_ptr_factory_{this};
};
}  // namespace ash::quick_start

#endif  // CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_CONNECTIVITY_FAKE_CONNECTION_H_

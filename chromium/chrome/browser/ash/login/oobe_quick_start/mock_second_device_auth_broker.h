// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_MOCK_SECOND_DEVICE_AUTH_BROKER_H_
#define CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_MOCK_SECOND_DEVICE_AUTH_BROKER_H_

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/ash/login/oobe_quick_start/second_device_auth_broker.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace ash::quick_start {

class MockSecondDeviceAuthBroker : public SecondDeviceAuthBroker {
 public:
  explicit MockSecondDeviceAuthBroker(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  MockSecondDeviceAuthBroker(const MockSecondDeviceAuthBroker&) = delete;
  MockSecondDeviceAuthBroker& operator=(const MockSecondDeviceAuthBroker&) =
      delete;
  ~MockSecondDeviceAuthBroker() override;

  MOCK_METHOD(void, GetChallengeBytes, (ChallengeBytesCallback), (override));
  MOCK_METHOD(void,
              FetchRefreshToken,
              (const FidoAssertionInfo&,
               const std::string&,
               RefreshTokenCallback),
              (override));
  MOCK_METHOD(void,
              FetchAttestationCertificate,
              (const std::string&, AttestationCertificateCallback),
              (override));
};

}  // namespace ash::quick_start

#endif  // CHROME_BROWSER_ASH_LOGIN_OOBE_QUICK_START_MOCK_SECOND_DEVICE_AUTH_BROKER_H_

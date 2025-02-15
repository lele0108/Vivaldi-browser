// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_SHARING_NEARBY_QUICK_START_DECODER_QUICK_START_DECODER_H_
#define CHROME_SERVICES_SHARING_NEARBY_QUICK_START_DECODER_QUICK_START_DECODER_H_

#include <vector>

#include "chromeos/ash/services/nearby/public/mojom/quick_start_decoder.mojom.h"
#include "chromeos/ash/services/nearby/public/mojom/quick_start_decoder_types.mojom-forward.h"
#include "chromeos/ash/services/nearby/public/mojom/quick_start_decoder_types.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ash::quick_start {

// QuickStartDecoder is a class on the utility process that will
// accept incoming raw bytes from an Android device, decode the
// bytes and parse them into secure structs that can be consumed
// by the browser process.
class QuickStartDecoder : public mojom::QuickStartDecoder {
 public:
  QuickStartDecoder(mojo::PendingReceiver<mojom::QuickStartDecoder> receiver,
                    base::OnceClosure on_disconnect);
  QuickStartDecoder(const QuickStartDecoder&) = delete;
  QuickStartDecoder& operator=(const QuickStartDecoder&) = delete;
  ~QuickStartDecoder() override;

  // mojom::QuickStartDecoder;
  void DecodeBootstrapConfigurations(
      const std::vector<uint8_t>& data,
      DecodeBootstrapConfigurationsCallback callback) override;

  void DecodeGetAssertionResponse(
      const std::vector<uint8_t>& data,
      DecodeGetAssertionResponseCallback callback) override;

  void DecodeWifiCredentialsResponse(
      const std::vector<uint8_t>& data,
      DecodeWifiCredentialsResponseCallback callback) override;

  void DecodeNotifySourceOfUpdateResponse(
      const std::vector<uint8_t>& data,
      DecodeNotifySourceOfUpdateResponseCallback callback) override;

  void DecodeUserVerificationResult(
      const std::vector<uint8_t>& data,
      DecodeUserVerificationResultCallback callback) override;

  void DecodeUserVerificationRequested(
      const std::vector<uint8_t>& data,
      DecodeUserVerificationRequestedCallback callback) override;
  // mojom::QuickStartDecoder:

 private:
  friend class QuickStartDecoderTest;
  void DoDecodeBootstrapConfigurations(
      const std::vector<uint8_t>& data,
      DecodeBootstrapConfigurationsCallback callback);
  mojom::GetAssertionResponsePtr DoDecodeGetAssertionResponse(
      const std::vector<uint8_t>& data);
  void DoDecodeWifiCredentialsResponse(
      const std::vector<uint8_t>& data,
      DecodeWifiCredentialsResponseCallback callback);
  absl::optional<std::vector<uint8_t>> ExtractFidoDataFromJsonResponse(
      const std::vector<uint8_t>& data);
  // If the kNotifySourceOfUpdateAckKey boolean is present in the response, this
  // method returns its value.
  absl::optional<bool> DoDecodeNotifySourceOfUpdateResponse(
      const std::vector<uint8_t>& data);
  mojo::Receiver<mojom::QuickStartDecoder> receiver_;
};

}  // namespace ash::quick_start

#endif  // CHROME_SERVICES_SHARING_NEARBY_QUICK_START_DECODER_QUICK_START_DECODER_H_

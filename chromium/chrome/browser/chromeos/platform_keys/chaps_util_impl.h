// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_PLATFORM_KEYS_CHAPS_UTIL_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_PLATFORM_KEYS_CHAPS_UTIL_IMPL_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "chrome/browser/chromeos/platform_keys/chaps_slot_session.h"
#include "chrome/browser/chromeos/platform_keys/chaps_util.h"
#include "chrome/browser/chromeos/platform_keys/pkcs12_reader.h"
#include "crypto/scoped_nss_types.h"

namespace chromeos {
namespace platform_keys {

// Default implementation of the ChapsUtil class. Communicates with the chapsd
// daemon using ChapsSlotSession. Should be used on a worker thread.
class ChapsUtilImpl : public ChapsUtil {
 public:
  ChapsUtilImpl(
      std::unique_ptr<ChapsSlotSessionFactory> chaps_slot_session_factory);
  ~ChapsUtilImpl() override;

  bool GenerateSoftwareBackedRSAKey(
      PK11SlotInfo* slot,
      uint16_t num_bits,
      crypto::ScopedSECKEYPublicKey* out_public_key,
      crypto::ScopedSECKEYPrivateKey* out_private_key) override;

  bool ImportPkcs12Certificate(PK11SlotInfo* slot,
                               const std::vector<uint8_t>& pkcs12_data,
                               const std::string& password,
                               bool is_software_backed) override;

  // Public for testing, allows replacing ChapsPkcs12Helper.
  bool ImportPkcs12CertificateImpl(
      PK11SlotInfo* slot,
      const std::vector<uint8_t>& pkcs12_data,
      const std::string& password,
      const bool is_software_backed,
      const Pkcs12Reader& pkcs12_helper1 = Pkcs12Reader());

  // If called with true, every slot is assumed to be a chaps-provided slot.
  void SetIsChapsProvidedSlotForTesting(
      bool is_chaps_provided_slot_for_testing) {
    is_chaps_provided_slot_for_testing_ = is_chaps_provided_slot_for_testing;
  }

 private:
  std::unique_ptr<ChapsSlotSession> GetChapsSlotSessionForSlot(
      PK11SlotInfo* slot);

  std::unique_ptr<ChapsSlotSessionFactory> const chaps_slot_session_factory_;

  // If true, every slot is assumed to be a chaps-provided slot.
  bool is_chaps_provided_slot_for_testing_;
};

}  // namespace platform_keys
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_PLATFORM_KEYS_CHAPS_UTIL_IMPL_H_

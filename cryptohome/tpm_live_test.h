// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test methods that run on a real TPM.
// Note: the TPM must be owned in order for all tests to work correctly.

#ifndef CRYPTOHOME_TPM_LIVE_TEST_H_
#define CRYPTOHOME_TPM_LIVE_TEST_H_

#include <map>
#include <string>

#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/tpm.h"

#include <base/logging.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

class TpmLiveTest {
 public:
  TpmLiveTest();
  TpmLiveTest(const TpmLiveTest&) = delete;
  TpmLiveTest& operator=(const TpmLiveTest&) = delete;

  ~TpmLiveTest() = default;

  // This method runs all of the tests.
  bool RunLiveTests();

 private:
  // Helper method to try to sign some data.
  bool SignData(const brillo::SecureBlob& pcr_bound_key,
                const brillo::SecureBlob& public_key_der,
                int index);

  // Helper method to try to encrypt and decrypt some data.
  bool EncryptAndDecryptData(const brillo::SecureBlob& pcr_bound_key,
                             const std::map<uint32_t, brillo::Blob>& pcr_map);

  // These tests check TPM-bound AuthBlocks work correctly.
  bool TpmEccAuthBlockTest();
  bool TpmBoundToPcrAuthBlockTest();
  bool TpmNotBoundToPcrAuthBlockTest();

  // This test checks if PCRs and PCR bound keys work correctly.
  bool PCRKeyTest();

  // This test checks if we can create and load an RSA decryption key and use
  // it to encrypt and decrypt.
  bool DecryptionKeyTest();

  // This test checks if we can seal and unseal a blob to a PCR state using
  // some authorization value.
  bool SealToPcrWithAuthorizationTest();

  // This test verifies that the Nvram subsystem of the TPM is working
  // correctly.
  bool NvramTest();

  // This test checks the signature-sealed secret creation and its unsealing. A
  // random RSA key is used.
  bool SignatureSealedSecretTest();

  FakePlatform platform_;
  Tpm* tpm_;
  CryptohomeKeysManager cryptohome_keys_manager_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_LIVE_TEST_H_

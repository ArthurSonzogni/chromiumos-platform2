// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_
#define CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_

#include <base/macros.h>

#include <string>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmAuthBlockUtils {
 public:
  TpmAuthBlockUtils(Tpm* tpm, CryptohomeKeyLoader* cryptohome_key_loader);
  TpmAuthBlockUtils(const TpmAuthBlockUtils&) = delete;
  TpmAuthBlockUtils& operator=(const TpmAuthBlockUtils&) = delete;

  // A static method which converts an error object.
  // |err| shouldn't be a nullptr.
  static CryptoError TPMErrorToCrypto(const hwsec::error::TPMErrorBase& err);

  // A static method to report which errors can be recovered from with a retry.
  // |err| shouldn't be a nullptr.
  static bool TPMErrorIsRetriable(const hwsec::error::TPMErrorBase& err);

  // Checks if the specified |hash| is the same as the hash for the |tpm_| used
  // by the class.
  CryptoError IsTPMPubkeyHash(const brillo::SecureBlob& hash) const;

  // This checks that the TPM is ready and that the vault keyset was encrypted
  // with this machine's TPM.
  CryptoError CheckTPMReadiness(bool has_tpm_key,
                                bool has_tpm_public_key_hash,
                                const brillo::SecureBlob& tpm_public_key_hash);

 private:
  Tpm* tpm_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_

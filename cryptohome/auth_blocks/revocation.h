// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_REVOCATION_H_
#define CRYPTOHOME_AUTH_BLOCKS_REVOCATION_H_

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/tpm.h"

namespace cryptohome {
namespace revocation {

bool IsRevocationSupported(Tpm* tpm);

// Derives a new key from `in_out_key_blobs.vkk_key` and saves it back to
// `in_out_key_blobs.vkk_key`. Saves information that is required for key
// derivation to `in_out_revocation_state`.
CryptoError Create(LECredentialManager* le_manager,
                   RevocationState* in_out_revocation_state,
                   KeyBlobs* in_out_key_blobs);

// Derives a new key from `in_out_key_blobs.vkk_key` using information from
// `revocation_state` and saves it back to `in_out_key_blobs.vkk_key`.
CryptoError Derive(LECredentialManager* le_manager,
                   const RevocationState& revocation_state,
                   KeyBlobs* in_out_key_blobs);

// Removes data required to derive a key from provided `revocation_state`.
CryptoError Revoke(LECredentialManager* le_manager,
                   const RevocationState& revocation_state);

}  // namespace revocation
}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_REVOCATION_H_

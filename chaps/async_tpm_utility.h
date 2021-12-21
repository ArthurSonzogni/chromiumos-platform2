// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_ASYNC_TPM_UTILITY_H_
#define CHAPS_ASYNC_TPM_UTILITY_H_

#include <string>

#include <base/bind.h>
#include <brillo/secure_blob.h>

#include "chaps/tpm_utility.h"

namespace chaps {

// AsyncTPMUtility is a high-level interface to TPM services with some extra
// asynchronous interfaces.
class AsyncTPMUtility : public TPMUtility {
 public:
  using GenerateRandomCallback =
      base::OnceCallback<void(bool, std::string random_data)>;
  using UnloadKeysForSlotCallback = base::OnceCallback<void()>;
  using SealDataCallback = base::OnceCallback<void(
      bool, std::string key_blob, std::string encrypted_data)>;
  using UnsealDataCallback =
      base::OnceCallback<void(bool, brillo::SecureBlob unsealed_data)>;

  virtual ~AsyncTPMUtility() {}

  // The asynchronous version TPMUtility::GenerateRandom.
  virtual void GenerateRandomAsync(int num_bytes,
                                   GenerateRandomCallback callback) = 0;

  // Unloads all keys loaded for a particular slot. All key handles for the
  // given slot will not be valid after the callback be called.
  virtual void UnloadKeysForSlotAsync(int slot,
                                      UnloadKeysForSlotCallback callback) = 0;

  // The asynchronous version TPMUtility::SealData.
  virtual void SealDataAsync(const std::string& unsealed_data,
                             const brillo::SecureBlob& auth_value,
                             SealDataCallback callback) = 0;

  // The asynchronous version TPMUtility::UnsealData.
  virtual void UnsealDataAsync(const std::string& key_blob,
                               const std::string& encrypted_data,
                               const brillo::SecureBlob& auth_value,
                               UnsealDataCallback callback) = 0;
};

}  // namespace chaps

#endif  // CHAPS_ASYNC_TPM_UTILITY_H_

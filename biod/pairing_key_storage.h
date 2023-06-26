// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_PAIRING_KEY_STORAGE_H_
#define BIOD_PAIRING_KEY_STORAGE_H_

#include <optional>

#include <brillo/secure_blob.h>

namespace biod {

// This class handles the persistent storage of the pairing key (Pk). It is
// only established once per powerwash cycle, and on every boot we need to load
// the Pk back to the AuthStack. Aside from the Pk itself, the AuthStack might
// need other key materials/metadata to load the Pk, and we put those in a
// PkInfo blob.
class PairingKeyStorage {
 public:
  virtual ~PairingKeyStorage() = default;

  virtual bool PairingKeyExists() = 0;
  virtual std::optional<brillo::Blob> ReadWrappedPairingKey() = 0;
  virtual bool WriteWrappedPairingKey(
      const brillo::Blob& wrapped_pairing_key) = 0;
};

}  // namespace biod

#endif  // BIOD_PAIRING_KEY_STORAGE_H_

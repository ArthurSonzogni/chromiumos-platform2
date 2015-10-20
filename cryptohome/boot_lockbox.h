// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOT_LOCKBOX_H_
#define CRYPTOHOME_BOOT_LOCKBOX_H_

#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "boot_lockbox_key.pb.h"  // NOLINT(build/include)

namespace cryptohome {

class Crypto;
class Platform;
class Tpm;

// This class implements a boot-lockbox using a TPM key which is restricted to a
// 0 value of TPM PCR 1.  Every boot this key can be used to sign data and until
// a user session starts at which time the PCR is extended and the key can no
// longer be used.  In this way the signed data is tamper-evident to all
// modifications except during the window right after boot.
//
// A normal usage flow for BootLockbox would be something like:
//
// BootLockbox lockbox(tpm);
// success = lockbox.Sign(data, &signature);
// ...
// lockbox.FinalizeBoot();
// ...
// success = lockbox.Verify(data, signature);
class BootLockbox {
 public:
  // Does not take ownership of pointers.
  BootLockbox(Tpm* tpm, Platform* platform, Crypto* crypto);
  virtual ~BootLockbox();

  // Signs |data| for boot-time tamper evidence.  This always fails after
  // FinalizeBoot() has been called.  On success returns true and sets the
  // |signature| value.  The signature scheme will be RSA-PKCS1-SHA256.
  virtual bool Sign(const brillo::SecureBlob& data,
                    brillo::SecureBlob* signature);

  // Verifies that |signature| is valid for |data| and that it was generated
  // before FinalizeBoot() on a current or prior boot.
  virtual bool Verify(const brillo::SecureBlob& data,
                      const brillo::SecureBlob& signature);

  // Locks the key used by Sign() so it cannot be used again until the next
  // boot.  Returns true on success.
  virtual bool FinalizeBoot();

  // Checks if BootLockbox has been finalized.
  virtual bool IsFinalized();

 protected:
  // Returns the TPM |key_blob| for the lockbox key.  Returns true on success.
  bool GetKeyBlob(brillo::SecureBlob* key_blob);

  // Returns the DER-encoded lockbox |public_key|.  Returns true on success.
  bool GetPublicKey(brillo::SecureBlob* public_key);

  // Returns the |creation_blob| for the lockbox key.  Returns true on success.
  bool GetCreationBlob(brillo::SecureBlob* creation_blob);

  // Loads the lockbox key protobuf from a file.  Returns true on success.
  bool LoadKey();

  // Saves the lockbox key protobuf to a file.  Returns true on success.
  bool SaveKey();

  // Creates a new lockbox key.  On success returns true and populates |key_|.
  bool CreateKey();

  // Verifies an RSA-PKCS1-SHA256 signature as created by Sign().
  bool VerifySignature(const brillo::SecureBlob& public_key,
                       const brillo::SecureBlob& signed_data,
                       const brillo::SecureBlob& signature);

  Tpm* tpm_;
  Platform* platform_;
  Crypto* crypto_;
  BootLockboxKey key_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BootLockbox);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOT_LOCKBOX_H_

// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_NSS_UTIL_H_
#define LOGIN_MANAGER_NSS_UTIL_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <base/macros.h>
#include <crypto/scoped_nss_types.h>

namespace base {
class FilePath;
}

namespace crypto {
class RSAPrivateKey;
}

namespace login_manager {
// Forward declaration.
typedef struct PK11SlotInfoStr PK11SlotInfo;

// An interface to wrap the usage of crypto/nss_util.h and allow for mocking.
class NssUtil {
 public:
  NssUtil();
  virtual ~NssUtil();

  // Creates an NssUtil, ownership returns to the caller. If there is no
  // Factory (the default) this creates and returns a new NssUtil.
  static NssUtil* Create();

  // Returns empty ScopedPK11Slot in the event that the database
  // cannot be opened.
  virtual crypto::ScopedPK11Slot OpenUserDB(
      const base::FilePath& user_homedir) = 0;

  // Caller takes ownership of returned key.
  virtual crypto::RSAPrivateKey* GetPrivateKeyForUser(
      const std::vector<uint8_t>& public_key_der,
      PK11SlotInfo* user_slot) = 0;

  // Caller takes ownership of returned key.
  virtual crypto::RSAPrivateKey* GenerateKeyPairForUser(
      PK11SlotInfo* user_slot) = 0;

  virtual base::FilePath GetOwnerKeyFilePath() = 0;

  // Returns subpath of the NSS DB; e.g. '.pki/nssdb'
  virtual base::FilePath GetNssdbSubpath() = 0;

  // Returns true if |blob| is a validly encoded NSS SubjectPublicKeyInfo.
  virtual bool CheckPublicKeyBlob(const std::vector<uint8_t>& blob) = 0;

  virtual bool Verify(const std::vector<uint8_t>& signature,
                      const std::vector<uint8_t>& data,
                      const std::vector<uint8_t>& public_key) = 0;

  virtual bool Sign(const std::vector<uint8_t>& data,
                    crypto::RSAPrivateKey* key,
                    std::vector<uint8_t>* out_signature) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(NssUtil);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_NSS_UTIL_H_

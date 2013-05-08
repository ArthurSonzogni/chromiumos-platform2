// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_NSS_UTIL_H_
#define LOGIN_MANAGER_NSS_UTIL_H_

#include <base/basictypes.h>
#include <string>
#include <vector>

namespace base {
class FilePath;
}

namespace crypto {
class RSAPrivateKey;
}

namespace login_manager {

// An interface to wrap the usage of crypto/nss_util.h and allow for mocking.
class NssUtil {
 public:

  NssUtil();
  virtual ~NssUtil();

  // Creates an NssUtil, ownership returns to the caller. If there is no
  // Factory (the default) this creates and returns a new NssUtil.
  static NssUtil* Create();

  static void BlobFromBuffer(const std::string& buf, std::vector<uint8>* out);

  virtual bool OpenUserDB(const base::FilePath& user_homedir) = 0;

  // Caller takes ownership of returned key.
  virtual crypto::RSAPrivateKey* GetPrivateKey(
      const std::vector<uint8>& public_key_der) = 0;

  // Caller takes ownership of returned key.
  virtual crypto::RSAPrivateKey* GenerateKeyPair() = 0;

  virtual base::FilePath GetOwnerKeyFilePath() = 0;

  // Returns subpath of the NSS DB; e.g. '.pki/nssdb'
  virtual base::FilePath GetNssdbSubpath() = 0;

  // Returns true if |blob| is a validly encoded NSS SubjectPublicKeyInfo.
  virtual bool CheckPublicKeyBlob(const std::vector<uint8>& blob) = 0;

  virtual bool Verify(const uint8* algorithm, int algorithm_len,
                      const uint8* signature, int signature_len,
                      const uint8* data, int data_len,
                      const uint8* public_key, int public_key_len) = 0;

  virtual bool Sign(const uint8* data, int data_len,
                    std::vector<uint8>* OUT_signature,
                    crypto::RSAPrivateKey* key) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(NssUtil);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_NSS_UTIL_H_

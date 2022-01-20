// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_SIGN_MANAGER_SIGN_MANAGER_H_
#define U2FD_SIGN_MANAGER_SIGN_MANAGER_H_

#include <string>
#include <vector>

#include <brillo/secure_blob.h>

namespace u2f {

enum class KeyType {
  kRsa,
  kEcc,
};

class SignManager {
 public:
  virtual ~SignManager() = default;

  virtual bool IsReady() = 0;
  virtual bool CreateKey(KeyType key_type,
                         const brillo::SecureBlob& auth_data,
                         std::string* key_blob,
                         std::vector<uint8_t>* public_key_cbor) = 0;
  virtual bool Sign(const std::string& key_blob,
                    const std::string& data_to_sign,
                    const brillo::SecureBlob& auth_data,
                    std::string* signature_der) = 0;
};

}  // namespace u2f

#endif  // U2FD_SIGN_MANAGER_SIGN_MANAGER_H_

// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/utility/crypto.h"

#include <limits>
#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace hwsec_foundation {
namespace utility {

brillo::SecureBlob CreateSecureRandomBlob(size_t length) {
  // OpenSSL takes a signed integer. Returns nullopt if the user requests
  // something too large.
  if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
    LOG(ERROR) << __func__ << ": length exceeds the limit of int.";
    return brillo::SecureBlob();
  }

  brillo::SecureBlob blob(length);
  if (!RAND_bytes(reinterpret_cast<unsigned char*>(blob.data()), length)) {
    LOG(ERROR) << __func__ << ": failed to generate " << length
               << " random bytes: " << GetOpensslError();
    return brillo::SecureBlob();
  }

  return blob;
}

std::string GetOpensslError() {
  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);
  char* data = nullptr;
  int data_len = BIO_get_mem_data(bio, &data);
  std::string error_string(data, data_len);
  BIO_free(bio);
  return error_string;
}

}  // namespace utility
}  // namespace hwsec_foundation

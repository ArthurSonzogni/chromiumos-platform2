// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_MOCK_CERTIFICATE_CHECKER_H_
#define UPDATE_ENGINE_MOCK_CERTIFICATE_CHECKER_H_

#include <gmock/gmock.h>
#include <openssl/ssl.h>

#include "update_engine/certificate_checker.h"

namespace chromeos_update_engine {

class MockOpenSSLWrapper : public OpenSSLWrapper {
 public:
  MOCK_CONST_METHOD4(GetCertificateDigest,
                     bool(X509_STORE_CTX* x509_ctx,
                          int* out_depth,
                          unsigned int* out_digest_length,
                          uint8_t* out_digest));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_MOCK_CERTIFICATE_CHECKER_H_

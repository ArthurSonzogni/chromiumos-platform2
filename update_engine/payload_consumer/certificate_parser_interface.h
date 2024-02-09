// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CERTIFICATE_PARSER_INTERFACE_H_
#define UPDATE_ENGINE_CERTIFICATE_PARSER_INTERFACE_H_

#include <memory>
#include <string>
#include <vector>

#include <openssl/pem.h>

namespace chromeos_update_engine {

// This class parses the PEM encoded X509 certificates from |path|; and
// passes the parsed public keys to the caller.
class CertificateParserInterface {
 public:
  virtual ~CertificateParserInterface() = default;

  virtual bool ReadPublicKeysFromCertificates(
      const std::string& path,
      std::vector<std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>>*
          out_public_keys) = 0;
};

std::unique_ptr<CertificateParserInterface> CreateCertificateParser();

}  // namespace chromeos_update_engine

#endif

// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/certificate_parser_stub.h"

namespace chromeos_update_engine {
bool CertificateParserStub::ReadPublicKeysFromCertificates(
    const std::string& path,
    std::vector<std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>>*
        out_public_keys) {
  return true;
}

std::unique_ptr<CertificateParserInterface> CreateCertificateParser() {
  return std::make_unique<CertificateParserStub>();
}

}  // namespace chromeos_update_engine

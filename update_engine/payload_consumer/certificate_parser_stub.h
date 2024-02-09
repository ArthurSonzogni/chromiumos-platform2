// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CERTIFICATE_PARSER_STUB_H_
#define UPDATE_ENGINE_CERTIFICATE_PARSER_STUB_H_

#include <memory>
#include <string>
#include <vector>

#include "update_engine/payload_consumer/certificate_parser_interface.h"

namespace chromeos_update_engine {
class CertificateParserStub : public CertificateParserInterface {
 public:
  CertificateParserStub() = default;
  CertificateParserStub(const CertificateParserStub&) = delete;
  CertificateParserStub& operator=(const CertificateParserStub&) = delete;

  bool ReadPublicKeysFromCertificates(
      const std::string& path,
      std::vector<std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>>*
          out_public_keys) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CERTIFICATE_PARSER_STUB_H_

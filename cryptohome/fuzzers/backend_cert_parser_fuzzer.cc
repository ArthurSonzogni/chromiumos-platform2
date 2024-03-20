// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/test_timeouts.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "cryptohome/recoverable_key_store/backend_cert_verify.h"

namespace cryptohome {
namespace {

// There are actual certificate XMLs with size ~7000. Set the max XML size to
// 10000.
constexpr size_t kMaxXmlSize = 10000;

// Performs initialization.
class Environment {
 public:
  Environment() {
    base::CommandLine::Init(0, nullptr);
    TestTimeouts::Initialize();
    // Suppress logging from the code under test.
    logging::SetMinLogLevel(logging::LOGGING_FATAL);
  }
};

}  // namespace

// Fuzz-tests the recoverable key store parser+verifier that takes input fetched
// from the internet.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider provider(data, size);

  std::string sig_xml = provider.ConsumeRandomLengthString(kMaxXmlSize);
  std::string cert_xml = provider.ConsumeRandomLengthString(kMaxXmlSize);
  // Call the 2 parse helper functions, and the verify-and-parse function used
  // by actual callers.
  ParseSignatureXml(sig_xml);
  ParseCertificateXml(cert_xml);
  VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);

  return 0;
}

}  // namespace cryptohome

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/fuzzed_proto_generator.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <openssl/rsa.h>

#include "policy/device_policy_impl.h"

namespace {

void GenerateRsaKey(crypto::ScopedEVP_PKEY& pkey, brillo::Blob& key_spki_der) {
  crypto::ScopedEVP_PKEY_CTX pkey_context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  CHECK(pkey_context);
  CHECK(EVP_PKEY_keygen_init(pkey_context.get()));
  CHECK(EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_context.get(), 2048));
  EVP_PKEY* pkey_raw = nullptr;
  CHECK(EVP_PKEY_keygen(pkey_context.get(), &pkey_raw));
  pkey.reset(pkey_raw);
  // Obtain the DER-encoded Subject Public Key Info.
  const int key_spki_der_length = i2d_PUBKEY(pkey.get(), nullptr);
  CHECK_GT(key_spki_der_length, 0);
  key_spki_der.resize(key_spki_der_length);
  uint8_t* key_spki_der_buffer = key_spki_der.data();
  CHECK(i2d_PUBKEY(pkey.get(), &key_spki_der_buffer) == key_spki_der.size());
}

// Performs initialization and holds state that's shared across all invocations
// of the fuzzer.
class Environment {
 public:
  Environment() {
    base::CommandLine::Init(0, nullptr);
    // Suppress log spam from the code-under-test.
    logging::SetMinLogLevel(logging::LOGGING_FATAL);
    GenerateRsaKey(pkey_, key_spki_der_);
  }

  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;

  const crypto::ScopedEVP_PKEY& pkey() const { return pkey_; }
  const brillo::Blob& key_spki_der() const { return key_spki_der_; }

 private:
  crypto::ScopedEVP_PKEY pkey_;
  brillo::Blob key_spki_der_;
};

std::string GeneratePolicyFileName(FuzzedDataProvider& fuzzed_data_provider) {
  std::string file_name = "policy";
  int max_suffix_count = fuzzed_data_provider.ConsumeIntegralInRange(0, 32);
  for (int i = 0; i < max_suffix_count; i++) {
    char c = fuzzed_data_provider.ConsumeIntegral<char>();
    // '\0' and '/' are invalid characters for linux file path.
    if (c == '\0') {
      break;
    }
    if (c == '/') {
      continue;
    }
    file_name += c;
  }
  return file_name;
}

brillo::Blob SignData(const crypto::ScopedEVP_PKEY& pkey,
                      const brillo::Blob& data) {
  CHECK(pkey);

  crypto::ScopedEVP_MD_CTX ctx(EVP_MD_CTX_new());
  CHECK(ctx);

  CHECK(EVP_SignInit(ctx.get(), EVP_sha1()));
  CHECK(EVP_SignUpdate(ctx.get(), data.data(), data.size()));

  brillo::Blob signature(EVP_PKEY_size(pkey.get()));
  unsigned int signature_size = 0;
  CHECK(
      EVP_SignFinal(ctx.get(), signature.data(), &signature_size, pkey.get()));
  signature.resize(signature_size);

  return signature;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider fuzzed_data_provider(data, size);
  brillo::FuzzedProtoGenerator proto_generator(fuzzed_data_provider);

  // Create the temporary directory.
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());

  base::FilePath policy_dir(temp_dir.GetPath());
  base::FilePath key_path(policy_dir.Append("owner.key"));

  bool verify_policy = fuzzed_data_provider.ConsumeBool();
  bool delete_invalid_files = fuzzed_data_provider.ConsumeBool();

  // Generate the key file.
  std::string key_data;
  // Using a valid/correct key or not.
  if (fuzzed_data_provider.ConsumeBool()) {
    key_data = brillo::BlobToString(env.key_spki_der());
  } else {
    key_data = fuzzed_data_provider.ConsumeRandomLengthString();
  }
  CHECK(base::WriteFile(key_path, key_data));

  // Generate random policy files.
  int policy_count = fuzzed_data_provider.ConsumeIntegralInRange(0, 10);
  for (int i = 0; i < policy_count; i++) {
    base::FilePath policy_file_path(
        policy_dir.Append(GeneratePolicyFileName(fuzzed_data_provider)));

    if (base::PathExists(policy_file_path)) {
      continue;
    }

    brillo::Blob policy_data = proto_generator.Generate();
    brillo::Blob signature = SignData(env.pkey(), policy_data);

    brillo::Blob result = brillo::FuzzedProtoGenerator(
                              {std::move(policy_data), std::move(signature)},
                              fuzzed_data_provider)
                              .Generate();

    CHECK(base::WriteFile(policy_file_path, brillo::BlobToString(result)));
  }

  base::FilePath policy_path(policy_dir.Append("policy"));

  // Set the necessary parameters for fuzzing.
  policy::DevicePolicyImpl device_policy;
  device_policy.set_policy_path_for_testing(policy_path);
  device_policy.set_key_file_path_for_testing(key_path);
  device_policy.set_verify_policy_for_testing(verify_policy);

  device_policy.LoadPolicy(delete_invalid_files);

  // TODO(b/316976956): Verify all Get...() accessors.
  return 0;
}

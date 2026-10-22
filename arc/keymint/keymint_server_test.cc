// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/keymint_server.h"

#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>
#include <hardware/keymaster_defs.h>
#include <mojo/keymint.mojom.h>

namespace arc::keymint {
namespace {

constexpr uint8_t kBlob1[] = {0x01, 0x02, 0x03};
constexpr uint8_t kBlob2[] = {0x04, 0x05, 0x06};

class KeyMintServerTest : public testing::Test {
 protected:
  KeyMintServerTest() = default;
  ~KeyMintServerTest() override = default;

  arc::mojom::keymint::CertificateRequestPtr CreateValidCertificateRequest() {
    auto request = arc::mojom::keymint::CertificateRequest::New();
    request->test_mode = false;
    request->keys_to_sign.push_back(arc::mojom::keymint::KeyMintBlob::New(
        std::vector<uint8_t>(std::begin(kBlob1), std::end(kBlob1))));
    request->encryption_cert_chain = arc::mojom::keymint::KeyMintBlob::New(
        std::vector<uint8_t>(std::begin(kBlob1), std::end(kBlob1)));
    request->challenge = arc::mojom::keymint::KeyMintBlob::New(
        std::vector<uint8_t>(std::begin(kBlob2), std::end(kBlob2)));
    return request;
  }

  arc::mojom::keymint::GenerateCertificateRequestResultOrErrorPtr
  GenerateCertificateRequest(
      arc::mojom::keymint::CertificateRequestPtr request) {
    base::test::TestFuture<
        arc::mojom::keymint::GenerateCertificateRequestResultOrErrorPtr>
        future;
    server_.GenerateCertificateRequest(std::move(request),
                                       future.GetCallback());
    return future.Take();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};
  KeyMintServer server_;
};

// Verifies that GenerateCertificateRequest invokes the callback with
// KM_ERROR_INVALID_ARGUMENT when the request pointer is null, rather than
// returning early and dropping the callback.
TEST_F(KeyMintServerTest, GenerateCertificateRequestNullRequest) {
  auto result = GenerateCertificateRequest(/*request=*/nullptr);

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
}

// Verifies that GenerateCertificateRequest invokes the callback when
// keys_to_sign is empty (such as when Android PeriodicProvisioner probes for a
// CSR with 0 keys).
TEST_F(KeyMintServerTest, GenerateCertificateRequestEmptyKeysToSign) {
  auto request = CreateValidCertificateRequest();
  request->keys_to_sign.clear();

  auto result = GenerateCertificateRequest(std::move(request));

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
}

// Verifies that GenerateCertificateRequest invokes the callback when an
// element in keys_to_sign is null or has empty data (failing
// MakeGenerateCsrRequest).
TEST_F(KeyMintServerTest, GenerateCertificateRequestNullOrEmptyKeyToSign) {
  for (bool is_null_key : {true, false}) {
    auto request = CreateValidCertificateRequest();
    request->keys_to_sign.clear();
    request->keys_to_sign.push_back(
        is_null_key ? nullptr : arc::mojom::keymint::KeyMintBlob::New());

    auto result = GenerateCertificateRequest(std::move(request));

    ASSERT_TRUE(result);
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
  }
}

// Verifies that GenerateCertificateRequest invokes the callback when
// challenge is null.
TEST_F(KeyMintServerTest, GenerateCertificateRequestNullChallenge) {
  auto request = CreateValidCertificateRequest();
  request->challenge = nullptr;

  auto result = GenerateCertificateRequest(std::move(request));

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
}

// Verifies that GenerateCertificateRequest invokes the callback when
// encryption_cert_chain is null or empty.
TEST_F(KeyMintServerTest,
       GenerateCertificateRequestNullOrEmptyEncryptionCertChain) {
  for (bool is_null_chain : {true, false}) {
    auto request = CreateValidCertificateRequest();
    request->encryption_cert_chain =
        is_null_chain ? nullptr : arc::mojom::keymint::KeyMintBlob::New();

    auto result = GenerateCertificateRequest(std::move(request));

    ASSERT_TRUE(result);
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
  }
}

// Verifies that GenerateCertificateRequest invokes the callback when the
// challenge blob is empty (failing MakeGenerateCsrRequest).
TEST_F(KeyMintServerTest, GenerateCertificateRequestEmptyChallenge) {
  auto request = CreateValidCertificateRequest();
  request->challenge = arc::mojom::keymint::KeyMintBlob::New();

  auto result = GenerateCertificateRequest(std::move(request));

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), KM_ERROR_INVALID_ARGUMENT);
}

}  // namespace
}  // namespace arc::keymint

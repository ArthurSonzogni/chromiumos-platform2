// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <libarc-attestation/lib/test_utils.h>

namespace arc::keymint::context {

namespace {
using testing::NiceMock;

constexpr uint32_t kP256SignatureLength = 64;

constexpr char kEcdsaDERSignatureHex[] =
    "304402202183f1eec06a7eca46e676562d3e4f440741ad517a5387c45c54a69a9da846ef02"
    "205d3760585055de67ca94b0e2136380956b95b0a783eaae3d0070f1d3ff71782f";

constexpr char kDKCertPEM[] = R"(-----BEGIN CERTIFICATE-----
MIIDIzCCAgugAwIBAgIWAY90AREo6PnvDXoULHkAAAAAAFZJ/TANBgkqhkiG9w0B
AQsFADCBhTEgMB4GA1UEAxMXUHJpdmFjeSBDQSBJbnRlcm1lZGlhdGUxEjAQBgNV
BAsTCUNocm9tZSBPUzETMBEGA1UEChMKR29vZ2xlIEluYzEWMBQGA1UEBxMNTW91
bnRhaW4gVmlldzETMBEGA1UECBMKQ2FsaWZvcm5pYTELMAkGA1UEBhMCVVMwHhcN
MjQwNTIzMjExOTQ1WhcNNDQwNTIzMjExOTQ1WjBLMS8wLQYDVQQKEyZBUkMgUmVt
b3RlIEtleSBQcm92aXNpb25pbmcgRGV2aWNlIEtleTEYMBYGA1UECxMPc3RhdGU6
ZGV2ZWxvcGVyMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEv/vqwnEBQPTFFzx8
Zoh1G1UnHFHP44I/OfJgmNSXPMgWuG3DNmbjx37NdLMvZDdOCmGO9rBLW4mYGw+s
1G4rpqOBjDCBiTApBgNVHQ4EIgQgryr7Nm+PvuYDdg5kgj5m8kwpHvhRV6N+fBn5
1Kq1Jo0wKwYDVR0jBCQwIoAg9CC22dhi9osJFc6LV6T8V064wXyl+eZW29BSlCm9
bX8wDgYDVR0PAQH/BAQDAgeAMAwGA1UdEwEB/wQCMAAwEQYDVR0gBAowCDAGBgRV
HSAAMA0GCSqGSIb3DQEBCwUAA4IBAQCSGfeftmQYFmWXhtZlCo+Otf4HnUUH460F
uvSqrvnndWVvB0F5Q7ZFkGKnWQkBc/UIXLttBpcIme389VwR+U2OJ8HNc1+aaGiy
QUJHfFMcIyLatHMrlzeqNaLvnKM6oRipQyI9gBT+N28FtZFdHpY2HRXZV6e37T4N
MrJz6UCWQv8KVcVhXVKhXlnifgFcAUc3ci76vbNRaNAHcrEV9qW3rJzzi2tUDieF
9cYnJ112Rd+zwQT3mqdD5m7SnBQy4xN5wRYZ/tcdNc3kQJPS3q/xykojEzUDSOEQ
XrqWjNtuK1n8SXwvWa7wq8h6sC5X801xluCzi0UcxyhKKCkAOd9D
-----END CERTIFICATE-----
)";

std::vector<uint8_t> convertHexToRawBytes(const char* hex_array) {
  std::string hex_string(kEcdsaDERSignatureHex);
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i < hex_string.length(); i += 2) {
    std::string byteString = hex_string.substr(i, 2);
    uint8_t byte =
        (uint8_t)strtol(byteString.c_str(), nullptr /* endPtr*/, 16 /*base*/);
    bytes.push_back(byte);
  }
  return bytes;
}
}  // namespace

class ArcRemoteProvisioningContextTest : public ::testing::Test {
 protected:
  ArcRemoteProvisioningContextTest() {}

  void SetUp() override {
    remote_provisioning_context_ =
        new ArcRemoteProvisioningContext(KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT);
  }

  void ExpectProvisionSuccess() {
    brillo::Blob dkCert = brillo::BlobFromString(kDKCertPEM);
    std::vector<brillo::Blob> kCertsOut{dkCert};

    EXPECT_CALL(*manager_, GetDkCertChain(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(kCertsOut),
            testing::Return(arc_attestation::AndroidStatus::ok())));
  }

  void ExpectSignSuccess(std::vector<uint8_t>& der_signature) {
    EXPECT_CALL(*manager_, SignWithP256Dk(testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(der_signature),
            testing::Return(arc_attestation::AndroidStatus::ok())));
  }

  void SetupManagerForTesting() {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
    arc_attestation::ArcAttestationManagerSingleton::CreateForTesting();
    std::unique_ptr<NiceMock<arc_attestation::MockArcAttestationManager>>
        manager = std::make_unique<
            NiceMock<arc_attestation::MockArcAttestationManager>>();
    manager_ = manager.get();
    arc_attestation::ArcAttestationManagerSingleton::Get()
        ->SetManagerForTesting(std::move(manager));
  }

  void TearDown() override {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
  }

  NiceMock<arc_attestation::MockArcAttestationManager>* manager_;
  ArcRemoteProvisioningContext* remote_provisioning_context_;
};

TEST_F(ArcRemoteProvisioningContextTest,
       createCoseSign1SignatureFromDKFailure) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  cppcose::ErrMsgOr<std::vector<uint8_t>> signature =
      createCoseSign1SignatureFromDK(protectedParams, payload, aad);

  // This should fail as we have not setup the Mock Arc Attestation manager yet.
  ASSERT_FALSE(signature);
}

TEST_F(ArcRemoteProvisioningContextTest, constructCoseSign1FromDKFailure) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  auto coseSign1 = constructCoseSign1FromDK(cppbor::Map(), payload, aad);

  // This should fail as we have not setup the Mock Arc Attestation manager yet.
  ASSERT_FALSE(coseSign1);
}

TEST_F(ArcRemoteProvisioningContextTest,
       createCoseSign1SignatureFromDKSuccess) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  // Prepare.
  SetupManagerForTesting();
  std::vector<uint8_t> byte_signature =
      convertHexToRawBytes(kEcdsaDERSignatureHex);
  ExpectSignSuccess(byte_signature);

  // Execute.
  cppcose::ErrMsgOr<std::vector<uint8_t>> signature =
      createCoseSign1SignatureFromDK(protectedParams, payload, aad);

  // Test.
  ASSERT_TRUE(signature);
  EXPECT_EQ(signature.moveValue().size(), kP256SignatureLength);
}

TEST_F(ArcRemoteProvisioningContextTest, GenerateBcc) {
  // Prepare.
  SetupManagerForTesting();
  ExpectProvisionSuccess();
  std::vector<uint8_t> byte_signature =
      convertHexToRawBytes(kEcdsaDERSignatureHex);
  ExpectSignSuccess(byte_signature);

  // Execute.
  auto bcc = remote_provisioning_context_->GenerateBcc(false);

  // Test.
  ASSERT_TRUE(bcc);
  auto coseKey = std::move(bcc->get(0));
  auto coseSign1 = std::move(bcc->get(1));
  const cppbor::Array* cose_sign = coseSign1->asArray();
  std::vector<uint8_t> additional_authenticated_data;
  ASSERT_TRUE(cppcose::verifyAndParseCoseSign1(cose_sign, coseKey->encode(),
                                               additional_authenticated_data));
}

}  // namespace arc::keymint::context

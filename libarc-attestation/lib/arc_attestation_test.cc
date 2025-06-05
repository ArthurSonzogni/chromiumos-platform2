// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <attestation/proto_bindings/interface.pb.h>
#include <libarc-attestation/lib/interface.h>
#include <libarc-attestation/lib/manager.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/factory/mock_factory.h>
#include <libhwsec/frontend/arc_attestation/mock_frontend.h>

// This needs to be after interface.pb.h due to protobuf dependency.
#include <attestation/dbus-proxy-mocks.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

using attestation::ACAType;
using attestation::AttestationStatus;
using attestation::CertificateProfile;
using attestation::GetCertificateReply;
using attestation::GetCertificateRequest;
using attestation::GetEndorsementInfoReply;
using attestation::GetEndorsementInfoRequest;
using attestation::KeyType;
using attestation::SignReply;
using attestation::SignRequest;
using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec::TPMError;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Ge;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;

namespace arc_attestation {

namespace {

constexpr char kSamplePEMCert[] = R"(-----BEGIN CERTIFICATE-----
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

constexpr char kSampleVerifiedCert[] = R"(-----BEGIN CERTIFICATE-----
MIIDEjCCAfqgAwIBAgIWAZc4EkLieeoULrNiBx0AAAAAADCNCzANBgkqhkiG9w0B
AQsFADCBhTEgMB4GA1UEAxMXUHJpdmFjeSBDQSBJbnRlcm1lZGlhdGUxEjAQBgNV
BAsTCUNocm9tZSBPUzETMBEGA1UEChMKR29vZ2xlIEluYzEWMBQGA1UEBxMNTW91
bnRhaW4gVmlldzETMBEGA1UECBMKQ2FsaWZvcm5pYTELMAkGA1UEBhMCVVMwHhcN
MjUwNjA2MjA1NjI4WhcNNDUwNjA2MjA1NjI4WjA6MR8wHQYDVQQKExZBUkMgVFBN
IENlcnRpZnlpbmcgS2V5MRcwFQYDVQQLEw5zdGF0ZTp2ZXJpZmllZDBZMBMGByqG
SM49AgEGCCqGSM49AwEHA0IABMcwK+682icKism5Lr5hK5r85vuH1DN9oKZ15Jkv
fj24V2WD3RNv19D5ApHytOARK9djjd5ck5PYz2mAezdCfP6jgYwwgYkwKQYDVR0O
BCIEIFNfhCKfiFWt68hoUirm18tEEk3URINT1L5lAO1iWuSZMCsGA1UdIwQkMCKA
IPQgttnYYvaLCRXOi1ek/FdOuMF8pfnmVtvQUpQpvW1/MA4GA1UdDwEB/wQEAwIH
gDAMBgNVHRMBAf8EAjAAMBEGA1UdIAQKMAgwBgYEVR0gADANBgkqhkiG9w0BAQsF
AAOCAQEApx1mJaZ/vU4doRyGqZSbwfVqDiqdsSGwbFGzPvDtM9d11iyTOyar2GG8
LpRs+udySc8WRboxBCt82nQ/lui0OUlS4bBdgAJeG8JppH4/tn+XQUsSKApj0//e
jYt/zYVVRpmXFikpQ/NdTdmNsz8CrCo9WS/4B8xG86shWuMfj6MQGmGtK/wvnHf7
nGnVD1Ana7iuwK7LcWbf4N6DRVhQI18mqI8rZPnUQYUJn4/RrtM4j0Ks/S+W1T8m
x8D2c/yj2wE+YnBWjFT8wZk03GvMnjsxd70uRzj1Ph9VBMcnNwkbd5Pe8fb73m6M
lwaYLmqWs2XwXnCS4ZU1jMf+jr+Oug==
-----END CERTIFICATE-----
)";
constexpr char kSamplePEMCertSubject[] =
    R"(/O=ARC Remote Key Provisioning Device Key/OU=state:developer)";
constexpr char kSamplePEMCertIssueDate[] = "May 23 21:19:45 2024 GMT";

// gmock matcher for protobufs, allowing to check protobuf arguments in mocks.
MATCHER_P(ProtobufEquals, expected_message, "") {
  std::string arg_dumped;
  arg.SerializeToString(&arg_dumped);
  std::string expected_message_dumped;
  expected_message.SerializeToString(&expected_message_dumped);
  return arg_dumped == expected_message_dumped;
}

}  // namespace

class ArcAttestationThreadedTest : public ::testing::Test {
 public:
  void SetUp() override {
    singleton_ = ArcAttestationManagerSingleton::CreateForTesting();
    std::unique_ptr<ArcAttestationManager> manager =
        std::make_unique<ArcAttestationManager>();
    manager_ = manager.get();
    singleton_->SetManagerForTesting(std::move(manager));

    manager_->Setup();

    std::unique_ptr<org::chromium::AttestationProxyMock> attestation_proxy =
        std::make_unique<org::chromium::AttestationProxyMock>();
    attestation_proxy_ = attestation_proxy.get();
    provisioner_ = manager_->GetProvisionerForTesting();
    provisioner_->SetAttestationProxyForTesting(std::move(attestation_proxy));
    version_attester_ = manager_->GetVersionAttesterForTesting();
    version_attester_->SetHwsecFactoryForTesting(&hwsec_factory_);

    default_hwsec_ = std::make_unique<hwsec::MockArcAttestationFrontend>();
    hwsec_ = default_hwsec_.get();
    ON_CALL(hwsec_factory_, GetArcAttestationFrontend())
        .WillByDefault(
            Invoke([this]() -> std::unique_ptr<hwsec::ArcAttestationFrontend> {
              CHECK(default_hwsec_);
              return std::move(default_hwsec_);
            }));
  }

  void TearDown() override {
    ArcAttestationManagerSingleton::DestroyForTesting();
    singleton_ = nullptr;
    manager_ = nullptr;
    attestation_proxy_ = nullptr;
    provisioner_ = nullptr;
    version_attester_ = nullptr;
    hwsec_ = nullptr;
    default_hwsec_.reset();
  }

 protected:
  void ExpectGetCertificateSuccess(const GetCertificateReply& reply,
                                   const GetCertificateRequest& request) {
    EXPECT_CALL(
        *attestation_proxy_,
        GetCertificate(_, _, _, Ge(kGetCertificateMinTimeout.InMilliseconds())))
        .WillRepeatedly(DoAll(SetArgPointee<1>(reply), Return(true)));
  }

  void ExpectSignSuccess(const SignReply& reply, const SignRequest& request) {
    EXPECT_CALL(*attestation_proxy_, Sign(ProtobufEquals(request), _, _,
                                          Ge(kSignMinTimeout.InMilliseconds())))
        .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));
  }

  void ExpectEndorsementKeySuccess(const GetEndorsementInfoReply& reply,
                                   const GetEndorsementInfoRequest& request) {
    EXPECT_CALL(*attestation_proxy_,
                GetEndorsementInfo(ProtobufEquals(request), _, _,
                                   Ge(kSignMinTimeout.InMilliseconds())))
        .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));
  }

  void SetupSuccessfulProvision() {
    // Setup the correct Android Device Key provision request expectation.
    GetCertificateRequest aadk_request;
    aadk_request.set_certificate_profile(
        CertificateProfile::ARC_ATTESTATION_DEVICE_KEY_CERTIFICATE);
    aadk_request.set_aca_type(ACAType::DEFAULT_ACA);
    aadk_request.set_key_type(KeyType::KEY_TYPE_ECC);
    aadk_request.set_key_label(kArcAttestationDeviceKeyLabel);
    aadk_request.set_shall_trigger_enrollment(true);
    aadk_request.set_forced(false);

    GetCertificateReply aadk_reply;
    aadk_reply.set_status(AttestationStatus::STATUS_SUCCESS);
    aadk_reply.set_certificate(std::string(kFakeCert1Part1) +
                               std::string(kFakeCert1Part2));
    aadk_reply.set_public_key(kFakePublicKey1);
    aadk_reply.set_key_blob(kFakeKeyBlob1);

    ExpectGetCertificateSuccess(aadk_reply, aadk_request);

    // Setup the correct TPM Certifying Key provision request expectation.
    GetCertificateRequest tck_request;
    tck_request.set_certificate_profile(
        CertificateProfile::ARC_TPM_CERTIFYING_KEY_CERTIFICATE);
    tck_request.set_aca_type(ACAType::DEFAULT_ACA);
    tck_request.set_key_type(KeyType::KEY_TYPE_ECC);
    tck_request.set_key_label(kTpmCertifyingKeyLabel);
    tck_request.set_shall_trigger_enrollment(true);
    tck_request.set_forced(false);

    GetCertificateReply tck_reply;
    tck_reply.set_status(AttestationStatus::STATUS_SUCCESS);
    tck_reply.set_certificate(std::string(kFakeCert2Part1) +
                              std::string(kFakeCert2Part2));
    tck_reply.set_public_key(kFakePublicKey2);
    tck_reply.set_key_blob(kFakeKeyBlob2);

    ExpectGetCertificateSuccess(tck_reply, tck_request);
  }

  ArcAttestationManagerSingleton* singleton_;
  ArcAttestationManager* manager_;
  org::chromium::AttestationProxyMock* attestation_proxy_;
  Provisioner* provisioner_;
  VersionAttester* version_attester_;
  hwsec::MockFactory hwsec_factory_;
  std::unique_ptr<hwsec::MockArcAttestationFrontend> default_hwsec_;
  hwsec::MockArcAttestationFrontend* hwsec_;

  static constexpr base::TimeDelta kGetCertificateMinTimeout =
      base::Seconds(60);
  static constexpr base::TimeDelta kSignMinTimeout = base::Seconds(15);

  static constexpr char kArcAttestationDeviceKeyLabel[] =
      "arc-attestation-device-key";
  static constexpr char kTpmCertifyingKeyLabel[] = "tpm-certifying-key";

  static constexpr char kFakeCert1Part1[] =
      "-----BEGIN CERTIFICATE-----\n"
      "part1-of-fake-cert-1\n"
      "-----END CERTIFICATE-----\n";
  static constexpr char kFakeCert1Part2[] =
      "-----BEGIN CERTIFICATE-----\n"
      "fake-cert-1-last-part\n"
      "-----END CERTIFICATE-----\n";
  static constexpr char kFakePublicKey1[] = "this-is-a-fake-public-key";
  static constexpr char kFakeKeyBlob1[] = "this-is-a-fake-key-blob";

  static constexpr char kFakeCert2Part1[] =
      "-----BEGIN CERTIFICATE-----\n"
      "ca-of-another-cert\n"
      "-----END CERTIFICATE-----\n";
  static constexpr char kFakeCert2Part2[] =
      "-----BEGIN CERTIFICATE-----\n"
      "the-last-part-of-another-cert\n"
      "-----END CERTIFICATE-----\n";
  static constexpr char kFakePublicKey2[] = "some-fake-ecc-public-key";
  static constexpr char kFakeKeyBlob2[] = "yet-another-fake-key-blob";

  static constexpr char kFakeSignData1[] = "to-be-signed";
  static constexpr char kFakeSignature1[] = "already-signed";

  static constexpr char kFakeChallenge[] = "can-you-answer-this?";
  static constexpr char kFakeLsbRelease[] = "SOME_VERSION=1.2.3";
  static constexpr char kFakeCmdline[] = "lsb_hash=AABBCC1234";
  static constexpr char kFakePcrQuote[] = "pcr-quoted";
  static constexpr char kFakePcrSignature[] = "pcr-signed";
};

TEST_F(ArcAttestationThreadedTest, ProvisionValidityTest) {
  SetupSuccessfulProvision();

  // Make the API call for provisioning.
  AndroidStatus result = ProvisionDkCert(true);
  ASSERT_TRUE(result.is_ok());

  // Test the resulting certificate.
  std::vector<Blob> cert_out;
  result = GetDkCertChain(cert_out);
  ASSERT_TRUE(result.is_ok());

  ASSERT_EQ(cert_out.size(), 2);
  EXPECT_EQ(BlobToString(cert_out[0]), kFakeCert2Part1);
  EXPECT_EQ(BlobToString(cert_out[1]), kFakeCert2Part2);

  // Test the signing.
  SignRequest sign_request;
  sign_request.set_key_label(kArcAttestationDeviceKeyLabel);
  sign_request.set_data_to_sign(kFakeSignData1);

  SignReply sign_reply;
  sign_reply.set_status(AttestationStatus::STATUS_SUCCESS);
  sign_reply.set_signature(kFakeSignature1);

  ExpectSignSuccess(sign_reply, sign_request);

  Blob data_to_sign = BlobFromString(kFakeSignData1);
  Blob result_signature;
  result = SignWithP256Dk(data_to_sign, result_signature);
  ASSERT_TRUE(result.is_ok());

  EXPECT_EQ(BlobToString(result_signature), kFakeSignature1);

  // Test the version attestation.
  arc_attestation::CrOSVersionAttestationBlob blob_to_return;
  blob_to_return.set_version(arc_attestation::CrOSVersionAttestationVersion::
                                 CROS_BLOB_VERSION_TPM2_FORMAT_1);
  blob_to_return.set_tpm_certifying_key_cert(std::string(kFakeCert2Part1) +
                                             std::string(kFakeCert2Part2));
  blob_to_return.set_lsb_release_content(kFakeLsbRelease);
  blob_to_return.set_kernel_cmdline_content(kFakeCmdline);
  blob_to_return.set_kernel_cmdline_quote(kFakePcrQuote);
  blob_to_return.set_kernel_cmdline_quote_signature(kFakePcrSignature);
  EXPECT_CALL(*hwsec_, AttestVersion(BlobFromString(kFakeKeyBlob2),
                                     std::string(kFakeCert2Part1) +
                                         std::string(kFakeCert2Part2),
                                     BlobFromString(kFakeChallenge)))
      .WillOnce(Return(blob_to_return));

  Blob quoted_blob;
  result = QuoteCrOSBlob(BlobFromString(kFakeChallenge), quoted_blob);
  ASSERT_TRUE(result.is_ok());
  arc_attestation::CrOSSpecificBlob quoted_data;
  quoted_data.ParseFromString(BlobToString(quoted_blob));
  EXPECT_EQ(quoted_data.version_attestation().version(),
            arc_attestation::CrOSVersionAttestationVersion::
                CROS_BLOB_VERSION_TPM2_FORMAT_1);
  EXPECT_EQ(quoted_data.version_attestation().tpm_certifying_key_cert(),
            std::string(kFakeCert2Part1) + std::string(kFakeCert2Part2));
  EXPECT_EQ(quoted_data.version_attestation().lsb_release_content(),
            kFakeLsbRelease);
  EXPECT_EQ(quoted_data.version_attestation().kernel_cmdline_content(),
            kFakeCmdline);
  EXPECT_EQ(quoted_data.version_attestation().kernel_cmdline_quote(),
            kFakePcrQuote);
  EXPECT_EQ(quoted_data.version_attestation().kernel_cmdline_quote_signature(),
            kFakePcrSignature);
}

TEST_F(ArcAttestationThreadedTest, ProvisionTCKFailed) {
  // Provisioning the TPM Certifying Key should fail.
  GetCertificateRequest tck_request;
  tck_request.set_certificate_profile(
      CertificateProfile::ARC_TPM_CERTIFYING_KEY_CERTIFICATE);
  tck_request.set_aca_type(ACAType::DEFAULT_ACA);
  tck_request.set_key_type(KeyType::KEY_TYPE_ECC);
  tck_request.set_key_label(kTpmCertifyingKeyLabel);
  tck_request.set_shall_trigger_enrollment(true);

  GetCertificateReply tck_reply;
  tck_reply.set_status(AttestationStatus::STATUS_UNEXPECTED_DEVICE_ERROR);

  ExpectGetCertificateSuccess(tck_reply, tck_request);

  // Make the API call for provisioning, and it should fail.
  AndroidStatus result = ProvisionDkCert(true);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, ProvisionDKFailed) {
  // Setup the correct TPM Certifying Key provision request expectation.
  GetCertificateRequest tck_request;
  tck_request.set_certificate_profile(
      CertificateProfile::ARC_TPM_CERTIFYING_KEY_CERTIFICATE);
  tck_request.set_aca_type(ACAType::DEFAULT_ACA);
  tck_request.set_key_type(KeyType::KEY_TYPE_ECC);
  tck_request.set_key_label(kTpmCertifyingKeyLabel);
  tck_request.set_shall_trigger_enrollment(true);

  GetCertificateReply tck_reply;
  tck_reply.set_status(AttestationStatus::STATUS_SUCCESS);
  tck_reply.set_certificate(std::string(kFakeCert2Part1) +
                            std::string(kFakeCert2Part2));
  tck_reply.set_public_key(kFakePublicKey2);
  tck_reply.set_key_blob(kFakeKeyBlob2);

  ExpectGetCertificateSuccess(tck_reply, tck_request);

  // Provisioning the Device Key should fail.
  GetCertificateRequest aadk_request;
  aadk_request.set_certificate_profile(
      CertificateProfile::ARC_ATTESTATION_DEVICE_KEY_CERTIFICATE);
  aadk_request.set_aca_type(ACAType::DEFAULT_ACA);
  aadk_request.set_key_type(KeyType::KEY_TYPE_ECC);
  aadk_request.set_key_label(kArcAttestationDeviceKeyLabel);
  aadk_request.set_shall_trigger_enrollment(true);

  GetCertificateReply aadk_reply;
  aadk_reply.set_status(AttestationStatus::STATUS_UNEXPECTED_DEVICE_ERROR);

  ExpectGetCertificateSuccess(aadk_reply, aadk_request);

  // Make the API call for provisioning, and it should fail.
  AndroidStatus result = ProvisionDkCert(true);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, NoCertWithoutProvision) {
  std::vector<Blob> cert_out;
  AndroidStatus result = GetDkCertChain(cert_out);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, SignFailed) {
  // We need to be provisioned to test this.
  SetupSuccessfulProvision();
  AndroidStatus result = ProvisionDkCert(true);
  ASSERT_TRUE(result.is_ok());

  // Setup signing failure.
  SignRequest sign_request;
  sign_request.set_key_label(kArcAttestationDeviceKeyLabel);
  sign_request.set_data_to_sign(kFakeSignData1);

  SignReply sign_reply;
  sign_reply.set_status(AttestationStatus::STATUS_UNEXPECTED_DEVICE_ERROR);

  ExpectSignSuccess(sign_reply, sign_request);

  // Call and it should fail.
  Blob data_to_sign = BlobFromString(kFakeSignData1);
  Blob result_signature;
  result = SignWithP256Dk(data_to_sign, result_signature);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, AttestVersionFailed) {
  // We need to be provisioned to test this.
  SetupSuccessfulProvision();
  AndroidStatus result = ProvisionDkCert(true);
  ASSERT_TRUE(result.is_ok());

  // AttestVersion should fail.
  EXPECT_CALL(*hwsec_, AttestVersion(BlobFromString(kFakeKeyBlob2),
                                     std::string(kFakeCert2Part1) +
                                         std::string(kFakeCert2Part2),
                                     BlobFromString(kFakeChallenge)))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  // Call to QuoteCrOSBlob should fail.
  Blob quoted_blob;
  result = QuoteCrOSBlob(BlobFromString(kFakeChallenge), quoted_blob);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, GetEndorsementKeySuccess) {
  // We need to be provisioned to test this.
  SetupSuccessfulProvision();
  AndroidStatus result = ProvisionDkCert(true);
  ASSERT_TRUE(result.is_ok());

  // Setup endorsement key success.
  GetEndorsementInfoRequest request;
  GetEndorsementInfoReply reply;
  reply.set_status(AttestationStatus::STATUS_SUCCESS);
  reply.set_ek_public_key(kFakePublicKey1);

  ExpectEndorsementKeySuccess(reply, request);

  // Call and it should succeed.
  Blob ek_public_key;
  result = GetEndorsementPublicKey(ek_public_key);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(ek_public_key, brillo::BlobFromString(kFakePublicKey1));
}

TEST_F(ArcAttestationThreadedTest, GetEndorsementKeyFailure) {
  // We need to be provisioned to test this.
  SetupSuccessfulProvision();
  AndroidStatus result = ProvisionDkCert(true);
  ASSERT_TRUE(result.is_ok());

  // Setup endorsement key failure.
  GetEndorsementInfoRequest request;
  GetEndorsementInfoReply reply;
  reply.set_status(AttestationStatus::STATUS_UNEXPECTED_DEVICE_ERROR);

  ExpectEndorsementKeySuccess(reply, request);

  // Call and it should fail.
  Blob ek_public_key;
  result = GetEndorsementPublicKey(ek_public_key);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(ArcAttestationThreadedTest, GetCertificateFieldsSuccess) {
  // Prepare.
  std::string pem_cert(kSamplePEMCert);
  std::string subject;
  std::string issue_date;

  // Execute.
  bool cert_fields_fetched =
      provisioner_->GetCertificateFields(pem_cert, &subject, &issue_date);

  // Test.
  ASSERT_TRUE(cert_fields_fetched);
  EXPECT_EQ(subject, kSamplePEMCertSubject);
  EXPECT_EQ(issue_date, kSamplePEMCertIssueDate);
}

TEST_F(ArcAttestationThreadedTest, CertShowsCorrectStateSuccess) {
  // Prepare.
  std::string pem_cert(kSampleVerifiedCert);

  // Execute.
  bool result = provisioner_->DoesCertShowCorrectState(pem_cert);

  // Test.
  ASSERT_TRUE(result);
}

TEST_F(ArcAttestationThreadedTest, CertShowsCorrectStateFailure) {
  // Prepare.
  std::string pem_cert(kSamplePEMCert);

  // Execute.
  bool result = provisioner_->DoesCertShowCorrectState(pem_cert);

  // Test.
  ASSERT_FALSE(result);
}

}  // namespace arc_attestation

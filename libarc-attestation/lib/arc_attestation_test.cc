// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <attestation/proto_bindings/interface.pb.h>
#include <libarc-attestation/lib/interface.h>
#include <libarc-attestation/lib/manager.h>
#include <libhwsec/factory/mock_factory.h>
#include <libhwsec/frontend/arc_attestation/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

// This needs to be after interface.pb.h due to protobuf dependency.
#include <attestation/dbus-proxy-mocks.h>

#include <gtest/gtest.h>

#include <base/strings/string_number_conversions.h>

using attestation::ACAType;
using attestation::AttestationStatus;
using attestation::CertificateProfile;
using attestation::GetCertificateReply;
using attestation::GetCertificateRequest;
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
    EXPECT_CALL(*attestation_proxy_,
                GetCertificate(ProtobufEquals(request), _, _,
                               Ge(kGetCertificateMinTimeout.InMilliseconds())))
        .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));
  }

  void ExpectSignSuccess(const SignReply& reply, const SignRequest& request) {
    EXPECT_CALL(*attestation_proxy_, Sign(ProtobufEquals(request), _, _,
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
  EXPECT_EQ(BlobToString(cert_out[0]), kFakeCert1Part1);
  EXPECT_EQ(BlobToString(cert_out[1]), kFakeCert1Part2);

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

}  // namespace arc_attestation

// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/bind.h>
#include <base/callback.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/data_encoding.h>
#include <chromeos/http/http_transport_fake.h>
#include <chromeos/mime_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/common/attestation_ca.pb.h"
#include "attestation/common/mock_crypto_utility.h"
#include "attestation/common/mock_tpm_utility.h"
#include "attestation/server/attestation_service.h"
#include "attestation/server/mock_database.h"
#include "attestation/server/mock_key_store.h"

using chromeos::http::fake::ServerRequest;
using chromeos::http::fake::ServerResponse;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgumentPointee;

namespace attestation {

class AttestationServiceTest : public testing::Test {
 public:
  enum FakeCAState {
    kSuccess,         // Valid successful response.
    kCommandFailure,  // Valid error response.
    kHttpFailure,     // Responds with an HTTP error.
    kBadMessageID,    // Valid successful response but a message ID mismatch.
  };

  ~AttestationServiceTest() override = default;
  void SetUp() override {
    service_.reset(new AttestationService);
    service_->set_database(&mock_database_);
    service_->set_crypto_utility(&mock_crypto_utility_);
    fake_http_transport_ = std::make_shared<chromeos::http::fake::Transport>();
    service_->set_http_transport(fake_http_transport_);
    service_->set_key_store(&mock_key_store_);
    service_->set_tpm_utility(&mock_tpm_utility_);
    // Setup a fake wrapped EK certificate by default.
    mock_database_.GetMutableProtobuf()->mutable_credentials()->
        mutable_default_encrypted_endorsement_credential()->
            set_wrapping_key_id("default");
    // Setup a fake Attestation CA for success by default.
    SetupFakeCAEnroll(kSuccess);
    SetupFakeCASign(kSuccess);
    CHECK(service_->Initialize());
  }

 protected:
  void SetupFakeCAEnroll(FakeCAState state) {
    fake_http_transport_->AddHandler(
        service_->attestation_ca_origin() + "/enroll",
        chromeos::http::request_type::kPost,
        base::Bind(&AttestationServiceTest::FakeCAEnroll,
                   base::Unretained(this),
                   state));
  }

  void SetupFakeCASign(FakeCAState state) {
    fake_http_transport_->AddHandler(
        service_->attestation_ca_origin() + "/sign",
        chromeos::http::request_type::kPost,
        base::Bind(&AttestationServiceTest::FakeCASign,
                   base::Unretained(this),
                   state));
  }

  std::string GetFakeCertificateChain() {
    const std::string kBeginCertificate = "-----BEGIN CERTIFICATE-----\n";
    const std::string kEndCertificate = "-----END CERTIFICATE-----";
    std::string pem = kBeginCertificate;
    pem += chromeos::data_encoding::Base64EncodeWrapLines("fake_cert");
    pem += kEndCertificate + "\n" + kBeginCertificate;
    pem += chromeos::data_encoding::Base64EncodeWrapLines("fake_ca_cert");
    pem += kEndCertificate + "\n" + kBeginCertificate;
    pem += chromeos::data_encoding::Base64EncodeWrapLines("fake_ca_cert2");
    pem += kEndCertificate;
    return pem;
  }

  CreateGoogleAttestedKeyRequest GetCreateRequest() {
    CreateGoogleAttestedKeyRequest request;
    request.set_key_label("label");
    request.set_key_type(KEY_TYPE_ECC);
    request.set_key_usage(KEY_USAGE_SIGN);
    request.set_certificate_profile(ENTERPRISE_MACHINE_CERTIFICATE);
    request.set_username("user");
    request.set_origin("origin");
    return request;
  }

  void Run() {
    run_loop_.Run();
  }

  void RunUntilIdle() {
    run_loop_.RunUntilIdle();
  }

  void Quit() {
    run_loop_.Quit();
  }

  std::shared_ptr<chromeos::http::fake::Transport> fake_http_transport_;
  NiceMock<MockCryptoUtility> mock_crypto_utility_;
  NiceMock<MockDatabase> mock_database_;
  NiceMock<MockKeyStore> mock_key_store_;
  NiceMock<MockTpmUtility> mock_tpm_utility_;
  std::unique_ptr<AttestationService> service_;

 private:
  void FakeCAEnroll(FakeCAState state,
                    const ServerRequest& request,
                    ServerResponse* response) {
    AttestationEnrollmentRequest request_pb;
    EXPECT_TRUE(request_pb.ParseFromString(request.GetDataAsString()));
    if (state == kHttpFailure) {
      response->ReplyText(chromeos::http::status_code::NotFound, std::string(),
                          chromeos::mime::application::kOctet_stream);
      return;
    }
    AttestationEnrollmentResponse response_pb;
    if (state == kCommandFailure) {
      response_pb.set_status(SERVER_ERROR);
      response_pb.set_detail("fake_enroll_error");
    } else if (state == kSuccess) {
      response_pb.set_status(OK);
      response_pb.set_detail("");
      response_pb.mutable_encrypted_identity_credential()->
          set_asym_ca_contents("1234");
      response_pb.mutable_encrypted_identity_credential()->
          set_sym_ca_attestation("5678");
    } else {
      NOTREACHED();
    }
    std::string tmp;
    response_pb.SerializeToString(&tmp);
    response->ReplyText(chromeos::http::status_code::Ok, tmp,
                        chromeos::mime::application::kOctet_stream);
  }

  void FakeCASign(FakeCAState state,
                  const ServerRequest& request,
                  ServerResponse* response) {
    AttestationCertificateRequest request_pb;
    EXPECT_TRUE(request_pb.ParseFromString(request.GetDataAsString()));
    if (state == kHttpFailure) {
      response->ReplyText(chromeos::http::status_code::NotFound, std::string(),
                          chromeos::mime::application::kOctet_stream);
      return;
    }
    AttestationCertificateResponse response_pb;
    if (state == kCommandFailure) {
      response_pb.set_status(SERVER_ERROR);
      response_pb.set_detail("fake_sign_error");
    } else if (state == kSuccess || state == kBadMessageID) {
      response_pb.set_status(OK);
      response_pb.set_detail("");
      if (state == kSuccess) {
        response_pb.set_message_id(request_pb.message_id());
      }
      response_pb.set_certified_key_credential("fake_cert");
      response_pb.set_intermediate_ca_cert("fake_ca_cert");
      *response_pb.add_additional_intermediate_ca_cert() = "fake_ca_cert2";
    }
    std::string tmp;
    response_pb.SerializeToString(&tmp);
    response->ReplyText(chromeos::http::status_code::Ok, tmp,
                        chromeos::mime::application::kOctet_stream);
  }

  base::MessageLoop message_loop_;
  base::RunLoop run_loop_;
};

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeySuccess) {
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(GetFakeCertificateChain(), reply.certificate_chain());
    EXPECT_FALSE(reply.has_server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeySuccessNoUser) {
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(GetFakeCertificateChain(), reply.certificate_chain());
    EXPECT_FALSE(reply.has_server_error());
    Quit();
  };
  CreateGoogleAttestedKeyRequest request = GetCreateRequest();
  request.clear_username();
  service_->CreateGoogleAttestedKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithEnrollHttpError) {
  SetupFakeCAEnroll(kHttpFailure);
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_CA_NOT_AVAILABLE, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithSignHttpError) {
  SetupFakeCASign(kHttpFailure);
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_CA_NOT_AVAILABLE, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithCAEnrollFailure) {
  SetupFakeCAEnroll(kCommandFailure);
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_REQUEST_DENIED_BY_CA, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("fake_enroll_error", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithCASignFailure) {
  SetupFakeCASign(kCommandFailure);
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_EQ(STATUS_REQUEST_DENIED_BY_CA, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("fake_sign_error", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithBadCAMessageID) {
  SetupFakeCASign(kBadMessageID);
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithNoEKCertificate) {
  // Remove the default credential setup.
  mock_database_.GetMutableProtobuf()->clear_credentials();
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithRNGFailure) {
  EXPECT_CALL(mock_crypto_utility_, GetRandom(_, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithRNGFailure2) {
  EXPECT_CALL(mock_crypto_utility_, GetRandom(_, _))
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithDBFailure) {
  EXPECT_CALL(mock_database_, SaveChanges()).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithDBFailureNoUser) {
  EXPECT_CALL(mock_database_, SaveChanges()).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  CreateGoogleAttestedKeyRequest request = GetCreateRequest();
  request.clear_username();
  service_->CreateGoogleAttestedKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithKeyWriteFailure) {
  EXPECT_CALL(mock_key_store_, Write(_, _, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithTpmNotReady) {
  EXPECT_CALL(mock_tpm_utility_, IsTpmReady())
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithTpmActivateFailure) {
  EXPECT_CALL(mock_tpm_utility_, ActivateIdentity(_, _, _, _, _, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyWithTpmCreateFailure) {
  EXPECT_CALL(mock_tpm_utility_, CreateCertifiedKey(_, _, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateGoogleAttestedKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_certificate_chain());
    EXPECT_EQ("", reply.server_error());
    Quit();
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyAndCancel) {
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [&callback_count](const CreateGoogleAttestedKeyReply& reply) {
    callback_count++;
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  // Bring down the service, which should cancel any callbacks.
  service_.reset();
  EXPECT_EQ(0, callback_count);
}

TEST_F(AttestationServiceTest, CreateGoogleAttestedKeyAndCancel2) {
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [&callback_count](const CreateGoogleAttestedKeyReply& reply) {
    callback_count++;
  };
  service_->CreateGoogleAttestedKey(GetCreateRequest(), base::Bind(callback));
  // Give threads a chance to run.
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
  // Bring down the service, which should cancel any callbacks.
  service_.reset();
  // Pump the loop to make sure no callbacks were posted.
  RunUntilIdle();
  EXPECT_EQ(0, callback_count);
}

TEST_F(AttestationServiceTest, GetKeyInfoSuccess) {
  // Setup a certified key in the key store.
  CertifiedKey key;
  key.set_public_key("public_key");
  key.set_certified_key_credential("fake_cert");
  key.set_intermediate_ca_cert("fake_ca_cert");
  *key.add_additional_intermediate_ca_cert() = "fake_ca_cert2";
  key.set_key_name("label");
  key.set_certified_key_info("certify_info");
  key.set_certified_key_proof("signature");
  key.set_key_type(KEY_TYPE_RSA);
  key.set_key_usage(KEY_USAGE_SIGN);
  std::string key_bytes;
  key.SerializeToString(&key_bytes);
  EXPECT_CALL(mock_key_store_, Read("user", "label", _))
      .WillOnce(DoAll(SetArgumentPointee<2>(key_bytes), Return(true)));

  // Set expectations on the outputs.
  auto callback = [this](const GetKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(KEY_TYPE_RSA, reply.key_type());
    EXPECT_EQ(KEY_USAGE_SIGN, reply.key_usage());
    EXPECT_EQ("public_key", reply.public_key());
    EXPECT_EQ("certify_info", reply.certify_info());
    EXPECT_EQ("signature", reply.certify_info_signature());
    EXPECT_EQ(GetFakeCertificateChain(), reply.certificate());
    Quit();
  };
  GetKeyInfoRequest request;
  request.set_key_label("label");
  request.set_username("user");
  service_->GetKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetKeyInfoSuccessNoUser) {
  // Setup a certified key in the device key store.
  CertifiedKey& key = *mock_database_.GetMutableProtobuf()->add_device_keys();
  key.set_public_key("public_key");
  key.set_certified_key_credential("fake_cert");
  key.set_intermediate_ca_cert("fake_ca_cert");
  *key.add_additional_intermediate_ca_cert() = "fake_ca_cert2";
  key.set_key_name("label");
  key.set_certified_key_info("certify_info");
  key.set_certified_key_proof("signature");
  key.set_key_type(KEY_TYPE_RSA);
  key.set_key_usage(KEY_USAGE_SIGN);

  // Set expectations on the outputs.
  auto callback = [this](const GetKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(KEY_TYPE_RSA, reply.key_type());
    EXPECT_EQ(KEY_USAGE_SIGN, reply.key_usage());
    EXPECT_EQ("public_key", reply.public_key());
    EXPECT_EQ("certify_info", reply.certify_info());
    EXPECT_EQ("signature", reply.certify_info_signature());
    EXPECT_EQ(GetFakeCertificateChain(), reply.certificate());
    Quit();
  };
  GetKeyInfoRequest request;
  request.set_key_label("label");
  service_->GetKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetKeyInfoNoKey) {
  EXPECT_CALL(mock_key_store_, Read("user", "label", _))
      .WillRepeatedly(Return(false));

  // Set expectations on the outputs.
  auto callback = [this](const GetKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_INVALID_PARAMETER, reply.status());
    Quit();
  };
  GetKeyInfoRequest request;
  request.set_key_label("label");
  request.set_username("user");
  service_->GetKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetKeyInfoBadPublicKey) {
  EXPECT_CALL(mock_crypto_utility_, GetRSASubjectPublicKeyInfo(_, _))
      .WillRepeatedly(Return(false));

  // Set expectations on the outputs.
  auto callback = [this](const GetKeyInfoReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    Quit();
  };
  GetKeyInfoRequest request;
  request.set_key_label("label");
  request.set_username("user");
  service_->GetKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetEndorsementInfoSuccess) {
  AttestationDatabase* database = mock_database_.GetMutableProtobuf();
  database->mutable_credentials()->set_endorsement_public_key("public_key");
  database->mutable_credentials()->set_endorsement_credential("certificate");
  // Set expectations on the outputs.
  auto callback = [this](const GetEndorsementInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("public_key", reply.ek_public_key());
    EXPECT_EQ("certificate", reply.ek_certificate());
    Quit();
  };
  GetEndorsementInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetEndorsementInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetEndorsementInfoNoInfo) {
  // Set expectations on the outputs.
  auto callback = [this](const GetEndorsementInfoReply& reply) {
    EXPECT_EQ(STATUS_NOT_AVAILABLE, reply.status());
    EXPECT_FALSE(reply.has_ek_public_key());
    EXPECT_FALSE(reply.has_ek_certificate());
    Quit();
  };
  GetEndorsementInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetEndorsementInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetEndorsementInfoNoCert) {
  AttestationDatabase* database = mock_database_.GetMutableProtobuf();
  database->mutable_credentials()->set_endorsement_public_key("public_key");
  // Set expectations on the outputs.
  auto callback = [this](const GetEndorsementInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("public_key", reply.ek_public_key());
    EXPECT_FALSE(reply.has_ek_certificate());
    Quit();
  };
  GetEndorsementInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetEndorsementInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetAttestationKeyInfoSuccess) {
  AttestationDatabase* database = mock_database_.GetMutableProtobuf();
  database->mutable_identity_key()->set_identity_public_key("public_key");
  database->mutable_identity_key()->set_identity_credential("certificate");
  database->mutable_pcr0_quote()->set_quote("pcr0");
  database->mutable_pcr1_quote()->set_quote("pcr1");
  database->mutable_identity_binding()->set_identity_public_key("public_key2");
  // Set expectations on the outputs.
  auto callback = [this](const GetAttestationKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("public_key", reply.public_key());
    EXPECT_EQ("public_key2", reply.public_key_tpm_format());
    EXPECT_EQ("certificate", reply.certificate());
    EXPECT_EQ("pcr0", reply.pcr0_quote().quote());
    EXPECT_EQ("pcr1", reply.pcr1_quote().quote());
    Quit();
  };
  GetAttestationKeyInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetAttestationKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetAttestationKeyInfoNoInfo) {
  // Set expectations on the outputs.
  auto callback = [this](const GetAttestationKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_NOT_AVAILABLE, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_public_key_tpm_format());
    EXPECT_FALSE(reply.has_certificate());
    EXPECT_FALSE(reply.has_pcr0_quote());
    EXPECT_FALSE(reply.has_pcr1_quote());
    Quit();
  };
  GetAttestationKeyInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetAttestationKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, GetAttestationKeyInfoSomeInfo) {
  AttestationDatabase* database = mock_database_.GetMutableProtobuf();
  database->mutable_identity_key()->set_identity_credential("certificate");
  database->mutable_pcr1_quote()->set_quote("pcr1");
  // Set expectations on the outputs.
  auto callback = [this](const GetAttestationKeyInfoReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_public_key_tpm_format());
    EXPECT_EQ("certificate", reply.certificate());
    EXPECT_FALSE(reply.has_pcr0_quote());
    EXPECT_EQ("pcr1", reply.pcr1_quote().quote());
    Quit();
  };
  GetAttestationKeyInfoRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  service_->GetAttestationKeyInfo(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, ActivateAttestationKeySuccess) {
  EXPECT_CALL(mock_database_, SaveChanges()).Times(1);
  EXPECT_CALL(mock_tpm_utility_, ActivateIdentity(_, _, _, "encrypted1",
                                                  "encrypted2", _))
      .WillOnce(DoAll(SetArgumentPointee<5>(std::string("certificate")),
                      Return(true)));
  // Set expectations on the outputs.
  auto callback = [this](const ActivateAttestationKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("certificate", reply.certificate());
    Quit();
  };
  ActivateAttestationKeyRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  request.mutable_encrypted_certificate()->set_asym_ca_contents("encrypted1");
  request.mutable_encrypted_certificate()->set_sym_ca_attestation("encrypted2");
  request.set_save_certificate(true);
  service_->ActivateAttestationKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, ActivateAttestationKeySuccessNoSave) {
  EXPECT_CALL(mock_database_, GetMutableProtobuf()).Times(0);
  EXPECT_CALL(mock_database_, SaveChanges()).Times(0);
  EXPECT_CALL(mock_tpm_utility_, ActivateIdentity(_, _, _, "encrypted1",
                                                  "encrypted2", _))
      .WillOnce(DoAll(SetArgumentPointee<5>(std::string("certificate")),
                      Return(true)));
  // Set expectations on the outputs.
  auto callback = [this](const ActivateAttestationKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("certificate", reply.certificate());
    Quit();
  };
  ActivateAttestationKeyRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  request.mutable_encrypted_certificate()->set_asym_ca_contents("encrypted1");
  request.mutable_encrypted_certificate()->set_sym_ca_attestation("encrypted2");
  request.set_save_certificate(false);
  service_->ActivateAttestationKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, ActivateAttestationKeySaveFailure) {
  EXPECT_CALL(mock_database_, SaveChanges()).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const ActivateAttestationKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    Quit();
  };
  ActivateAttestationKeyRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  request.mutable_encrypted_certificate()->set_asym_ca_contents("encrypted1");
  request.mutable_encrypted_certificate()->set_sym_ca_attestation("encrypted2");
  request.set_save_certificate(true);
  service_->ActivateAttestationKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, ActivateAttestationKeyActivateFailure) {
  EXPECT_CALL(mock_tpm_utility_, ActivateIdentity(_, _, _, "encrypted1",
                                                  "encrypted2", _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const ActivateAttestationKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    Quit();
  };
  ActivateAttestationKeyRequest request;
  request.set_key_type(KEY_TYPE_RSA);
  request.mutable_encrypted_certificate()->set_asym_ca_contents("encrypted1");
  request.mutable_encrypted_certificate()->set_sym_ca_attestation("encrypted2");
  request.set_save_certificate(true);
  service_->ActivateAttestationKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeySuccess) {
  // Configure a fake TPM response.
  EXPECT_CALL(mock_tpm_utility_, CreateCertifiedKey(KEY_TYPE_ECC,
                                                    KEY_USAGE_SIGN,
                                                    _, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgumentPointee<5>(std::string("public_key")),
                      SetArgumentPointee<7>(std::string("certify_info")),
                      SetArgumentPointee<8>(
                          std::string("certify_info_signature")),
                      Return(true)));
  // Expect the key to be written exactly once.
  EXPECT_CALL(mock_key_store_, Write("user", "label", _)).Times(1);
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("public_key", reply.public_key());
    EXPECT_EQ("certify_info", reply.certify_info());
    EXPECT_EQ("certify_info_signature", reply.certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  request.set_username("user");
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeySuccessNoUser) {
  // Configure a fake TPM response.
  EXPECT_CALL(mock_tpm_utility_, CreateCertifiedKey(KEY_TYPE_ECC,
                                                    KEY_USAGE_SIGN,
                                                    _, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgumentPointee<5>(std::string("public_key")),
                      SetArgumentPointee<7>(std::string("certify_info")),
                      SetArgumentPointee<8>(
                          std::string("certify_info_signature")),
                      Return(true)));
  // Expect the key to be written exactly once.
  EXPECT_CALL(mock_database_, SaveChanges()).Times(1);
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ("public_key", reply.public_key());
    EXPECT_EQ("certify_info", reply.certify_info());
    EXPECT_EQ("certify_info_signature", reply.certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeyRNGFailure) {
  EXPECT_CALL(mock_crypto_utility_, GetRandom(_, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_certify_info());
    EXPECT_FALSE(reply.has_certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeyTpmCreateFailure) {
  EXPECT_CALL(mock_tpm_utility_, CreateCertifiedKey(_, _, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_certify_info());
    EXPECT_FALSE(reply.has_certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeyDBFailure) {
  EXPECT_CALL(mock_key_store_, Write(_, _, _)).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_certify_info());
    EXPECT_FALSE(reply.has_certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  request.set_username("username");
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, CreateCertifiableKeyDBFailureNoUser) {
  EXPECT_CALL(mock_database_, SaveChanges()).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const CreateCertifiableKeyReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_public_key());
    EXPECT_FALSE(reply.has_certify_info());
    EXPECT_FALSE(reply.has_certify_info_signature());
    Quit();
  };
  CreateCertifiableKeyRequest request;
  request.set_key_label("label");
  request.set_key_type(KEY_TYPE_ECC);
  request.set_key_usage(KEY_USAGE_SIGN);
  service_->CreateCertifiableKey(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, DecryptSuccess) {
  // Set expectations on the outputs.
  auto callback = [this](const DecryptReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(MockTpmUtility::Transform("Unbind", "data"),
              reply.decrypted_data());
    Quit();
  };
  DecryptRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_encrypted_data("data");
  service_->Decrypt(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, DecryptSuccessNoUser) {
  mock_database_.GetMutableProtobuf()->add_device_keys()->set_key_name("label");
  // Set expectations on the outputs.
  auto callback = [this](const DecryptReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(MockTpmUtility::Transform("Unbind", "data"),
              reply.decrypted_data());
    Quit();
  };
  DecryptRequest request;
  request.set_key_label("label");
  request.set_encrypted_data("data");
  service_->Decrypt(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, DecryptKeyNotFound) {
  EXPECT_CALL(mock_key_store_, Read("user", "label", _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const DecryptReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_decrypted_data());
    Quit();
  };
  DecryptRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_encrypted_data("data");
  service_->Decrypt(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, DecryptKeyNotFoundNoUser) {
  // Set expectations on the outputs.
  auto callback = [this](const DecryptReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_decrypted_data());
    Quit();
  };
  DecryptRequest request;
  request.set_key_label("label");
  request.set_encrypted_data("data");
  service_->Decrypt(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, DecryptUnbindFailure) {
  EXPECT_CALL(mock_tpm_utility_, Unbind(_, _, _)).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const DecryptReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_decrypted_data());
    Quit();
  };
  DecryptRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_encrypted_data("data");
  service_->Decrypt(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, SignSuccess) {
  // Set expectations on the outputs.
  auto callback = [this](const SignReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(MockTpmUtility::Transform("Sign", "data"), reply.signature());
    Quit();
  };
  SignRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_data_to_sign("data");
  service_->Sign(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, SignSuccessNoUser) {
  mock_database_.GetMutableProtobuf()->add_device_keys()->set_key_name("label");
  // Set expectations on the outputs.
  auto callback = [this](const SignReply& reply) {
    EXPECT_EQ(STATUS_SUCCESS, reply.status());
    EXPECT_EQ(MockTpmUtility::Transform("Sign", "data"), reply.signature());
    Quit();
  };
  SignRequest request;
  request.set_key_label("label");
  request.set_data_to_sign("data");
  service_->Sign(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, SignKeyNotFound) {
  EXPECT_CALL(mock_key_store_, Read("user", "label", _))
      .WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const SignReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_signature());
    Quit();
  };
  SignRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_data_to_sign("data");
  service_->Sign(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, SignKeyNotFoundNoUser) {
  // Set expectations on the outputs.
  auto callback = [this](const SignReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_signature());
    Quit();
  };
  SignRequest request;
  request.set_key_label("label");
  request.set_data_to_sign("data");
  service_->Sign(request, base::Bind(callback));
  Run();
}

TEST_F(AttestationServiceTest, SignUnbindFailure) {
  EXPECT_CALL(mock_tpm_utility_, Sign(_, _, _)).WillRepeatedly(Return(false));
  // Set expectations on the outputs.
  auto callback = [this](const SignReply& reply) {
    EXPECT_NE(STATUS_SUCCESS, reply.status());
    EXPECT_FALSE(reply.has_signature());
    Quit();
  };
  SignRequest request;
  request.set_key_label("label");
  request.set_username("user");
  request.set_data_to_sign("data");
  service_->Sign(request, base::Bind(callback));
  Run();
}

}  // namespace attestation

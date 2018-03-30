//
// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <vector>

#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/common/tpm_utility_v2.h"
#include "tpm_manager/common/mock_tpm_nvram_interface.h"
#include "tpm_manager/common/mock_tpm_ownership_interface.h"
#include "tpm_manager/common/tpm_manager_constants.h"
#include "trunks/mock_blob_parser.h"
#include "trunks/mock_tpm.h"
#include "trunks/mock_tpm_utility.h"
#include "trunks/trunks_factory_for_test.h"

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::WithArg;
using ::trunks::TPM_RC_FAILURE;
using ::trunks::TPM_RC_SUCCESS;

const char kDefaultPassword[] = "password";

const char kValidModulusHex[] =
    "961037BC12D2A298BEBF06B2D5F8C9B64B832A2237F8CF27D5F96407A6041A4D"
    "AD383CB5F88E625F412E8ACD5E9D69DF0F4FA81FCE7955829A38366CBBA5A2B1"
    "CE3B48C14B59E9F094B51F0A39155874C8DE18A0C299EBF7A88114F806BE4F25"
    "3C29A509B10E4B19E31675AFE3B2DA77077D94F43D8CE61C205781ED04D183B4"
    "C349F61B1956C64B5398A3A98FAFF17D1B3D9120C832763EDFC8F4137F6EFBEF"
    "46D8F6DE03BD00E49DEF987C10BDD5B6F8758B6A855C23C982DDA14D8F0F2B74"
    "E6DEFA7EEE5A6FC717EB0FF103CB8049F693A2C8A5039EF1F5C025DC44BD8435"
    "E8D8375DADE00E0C0F5C196E04B8483CC98B1D5B03DCD7E0048B2AB343FFC11F";

std::string HexDecode(const std::string hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(reinterpret_cast<char*>(output.data()), output.size());
}

}  // namespace

namespace attestation {

class TpmUtilityTest : public testing::Test {
 public:
  ~TpmUtilityTest() override = default;
  void SetUp() override {
    // Setup default status data.
    tpm_status_.set_status(tpm_manager::STATUS_SUCCESS);
    tpm_status_.set_enabled(true);
    tpm_status_.set_owned(true);
    tpm_status_.mutable_local_data()->set_endorsement_password(
        kDefaultPassword);
    ON_CALL(mock_tpm_owner_, GetTpmStatus(_, _))
        .WillByDefault(
            WithArg<1>(Invoke(this, &TpmUtilityTest::FakeGetTpmStatus)));
    ON_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _))
        .WillByDefault(Invoke(this,
                              &TpmUtilityTest::FakeRemoveOwnerDependency));
    // Setup fake nvram.
    ON_CALL(mock_tpm_nvram_, ReadSpace(_, _))
        .WillByDefault(Invoke(this, &TpmUtilityTest::FakeReadSpace));
    // Setup a default public key.
    ON_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(GetValidPublicKey(nullptr)),
                             Return(TPM_RC_SUCCESS)));
    // Setup trunks factory with mocks.
    trunks_factory_for_test_.set_tpm(&mock_tpm_);
    trunks_factory_for_test_.set_tpm_utility(&mock_tpm_utility_);
    trunks_factory_for_test_.set_blob_parser(&mock_blob_parser_);
    tpm_utility_.reset(new TpmUtilityV2(&mock_tpm_owner_, &mock_tpm_nvram_,
                                        &trunks_factory_for_test_));
    tpm_utility_->Initialize();
  }

 protected:
  void FakeGetTpmStatus(
      const tpm_manager::TpmOwnershipInterface::GetTpmStatusCallback&
          callback) {
    callback.Run(tpm_status_);
  }

  void FakeReadSpace(
      const tpm_manager::ReadSpaceRequest& request,
      const tpm_manager::TpmNvramInterface::ReadSpaceCallback& callback) {
    last_read_space_request_ = request;
    callback.Run(next_read_space_reply_);
  }

  void FakeRemoveOwnerDependency(
      const tpm_manager::RemoveOwnerDependencyRequest& request,
      const tpm_manager::TpmOwnershipInterface::RemoveOwnerDependencyCallback&
          callback) {
    last_remove_dependency_request_ = request;
    callback.Run(next_remove_dependency_reply_);
  }

  trunks::TPMT_PUBLIC GetValidPublicKey(std::string* bytes_when_serialized) {
    trunks::TPMT_PUBLIC public_area;
    memset(&public_area, 0, sizeof(public_area));
    public_area.type = trunks::TPM_ALG_RSA;
    public_area.name_alg = trunks::TPM_ALG_SHA256;
    public_area.parameters.rsa_detail.key_bits = 2048;
    public_area.unique.rsa =
        trunks::Make_TPM2B_PUBLIC_KEY_RSA(HexDecode(kValidModulusHex));
    if (bytes_when_serialized) {
      Serialize_TPMT_PUBLIC(public_area, bytes_when_serialized);
    }
    return public_area;
  }

  tpm_manager::GetTpmStatusReply tpm_status_;
  tpm_manager::ReadSpaceRequest last_read_space_request_;
  tpm_manager::ReadSpaceReply next_read_space_reply_;
  tpm_manager::RemoveOwnerDependencyRequest last_remove_dependency_request_;
  tpm_manager::RemoveOwnerDependencyReply next_remove_dependency_reply_;

  NiceMock<tpm_manager::MockTpmOwnershipInterface> mock_tpm_owner_;
  NiceMock<tpm_manager::MockTpmNvramInterface> mock_tpm_nvram_;
  NiceMock<trunks::MockTpm> mock_tpm_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  NiceMock<trunks::MockBlobParser> mock_blob_parser_;
  trunks::TrunksFactoryForTest trunks_factory_for_test_;
  std::unique_ptr<TpmUtilityV2> tpm_utility_;
};

TEST_F(TpmUtilityTest, IsTpmReady) {
  EXPECT_TRUE(tpm_utility_->IsTpmReady());
}

TEST_F(TpmUtilityTest, IsTpmReadyNotOwned) {
  tpm_status_.set_owned(false);
  EXPECT_FALSE(tpm_utility_->IsTpmReady());
}

TEST_F(TpmUtilityTest, ActivateIdentity) {
  trunks::TPM2B_DIGEST fake_credential =
      trunks::Make_TPM2B_DIGEST("fake_credential");
  EXPECT_CALL(mock_tpm_, ActivateCredentialSync(_, _, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(fake_credential), Return(TPM_RC_SUCCESS)));
  std::string credential;
  EXPECT_TRUE(tpm_utility_->ActivateIdentityForTpm2(
      KEY_TYPE_RSA, "fake_identity_blob", "seed", "mac", "wrapped",
      &credential));
  EXPECT_EQ("fake_credential", credential);
}

TEST_F(TpmUtilityTest, ActivateIdentityFailLoadIdentityKey) {
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, LoadKey("fake_identity_blob", _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  std::string credential;
  EXPECT_FALSE(tpm_utility_->ActivateIdentityForTpm2(
      KEY_TYPE_RSA, "fake_identity_blob", "seed", "mac", "wrapped",
      &credential));
  EXPECT_TRUE(credential.empty());
}

TEST_F(TpmUtilityTest, ActivateIdentityFailLoadEndorsementKey) {
  EXPECT_CALL(mock_tpm_utility_, GetEndorsementKey(_, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  std::string credential;
  EXPECT_FALSE(tpm_utility_->ActivateIdentityForTpm2(
      KEY_TYPE_RSA, "fake_identity_blob", "seed", "mac", "wrapped",
      &credential));
  EXPECT_TRUE(credential.empty());
}

TEST_F(TpmUtilityTest, ActivateIdentityNoEndorsementPassword) {
  tpm_status_.mutable_local_data()->clear_endorsement_password();
  std::string credential;
  EXPECT_FALSE(tpm_utility_->ActivateIdentityForTpm2(
      KEY_TYPE_RSA, "fake_identity_blob", "seed", "mac", "wrapped",
      &credential));
  EXPECT_TRUE(credential.empty());
}

TEST_F(TpmUtilityTest, ActivateIdentityError) {
  EXPECT_CALL(mock_tpm_, ActivateCredentialSync(_, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  std::string credential;
  EXPECT_FALSE(tpm_utility_->ActivateIdentityForTpm2(
      KEY_TYPE_RSA, "fake_identity_blob", "seed", "mac", "wrapped",
      &credential));
  EXPECT_TRUE(credential.empty());
}

TEST_F(TpmUtilityTest, CreateCertifiedKey) {
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<8>("fake_key_blob"), Return(TPM_RC_SUCCESS)));
  trunks::TPM2B_DATA external_data;
  trunks::TPMT_SIG_SCHEME scheme;
  trunks::TPM2B_ATTEST fake_certify_info =
      trunks::Make_TPM2B_ATTEST("fake_attest");
  trunks::TPMT_SIGNATURE fake_signature;
  fake_signature.sig_alg = trunks::TPM_ALG_RSASSA;
  fake_signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_proof");
  EXPECT_CALL(mock_tpm_, CertifySync(_, _, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<4>(&external_data), SaveArg<5>(&scheme),
                      SetArgPointee<6>(fake_certify_info),
                      SetArgPointee<7>(fake_signature),
                      Return(TPM_RC_SUCCESS)));
  std::string key_blob;
  std::string public_key;
  std::string public_key_tpm_format;
  std::string key_info;
  std::string proof;
  EXPECT_TRUE(tpm_utility_->CreateCertifiedKey(
      KEY_TYPE_RSA, KEY_USAGE_SIGN, "fake_identity_blob", "fake_external_data",
      &key_blob, &public_key, &public_key_tpm_format, &key_info, &proof));
  EXPECT_EQ("fake_key_blob", key_blob);
  EXPECT_EQ("fake_attest", key_info);
  EXPECT_NE(std::string::npos, proof.find("fake_proof"));
  EXPECT_EQ("fake_external_data", trunks::StringFrom_TPM2B_DATA(external_data));
  EXPECT_EQ(trunks::TPM_ALG_RSASSA, scheme.scheme);
  EXPECT_EQ(trunks::TPM_ALG_SHA256, scheme.details.rsassa.hash_alg);
}

TEST_F(TpmUtilityTest, CreateCertifiedKeyFailCreate) {
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string key_blob;
  std::string public_key;
  std::string public_key_tpm_format;
  std::string key_info;
  std::string proof;
  EXPECT_FALSE(tpm_utility_->CreateCertifiedKey(
      KEY_TYPE_RSA, KEY_USAGE_SIGN, "fake_identity_blob", "fake_external_data",
      &key_blob, &public_key, &public_key_tpm_format, &key_info, &proof));
  EXPECT_EQ("", key_blob);
}

TEST_F(TpmUtilityTest, CreateCertifiedKeyFailCertify) {
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<8>("fake_key_blob"), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_, CertifySync(_, _, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string key_blob;
  std::string public_key;
  std::string public_key_tpm_format;
  std::string key_info;
  std::string proof;
  EXPECT_FALSE(tpm_utility_->CreateCertifiedKey(
      KEY_TYPE_RSA, KEY_USAGE_SIGN, "fake_identity_blob", "fake_external_data",
      &key_blob, &public_key, &public_key_tpm_format, &key_info, &proof));
}

TEST_F(TpmUtilityTest, SealToPCR0) {
  EXPECT_CALL(mock_tpm_utility_, SealData("fake_data", _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>("fake_sealed"), Return(TPM_RC_SUCCESS)));
  std::string sealed_data;
  EXPECT_TRUE(tpm_utility_->SealToPCR0("fake_data", &sealed_data));
  EXPECT_EQ("fake_sealed", sealed_data);
}

TEST_F(TpmUtilityTest, SealToPCR0Fail) {
  EXPECT_CALL(mock_tpm_utility_, SealData("fake_data", _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string sealed_data;
  EXPECT_FALSE(tpm_utility_->SealToPCR0("fake_data", &sealed_data));
  EXPECT_EQ("", sealed_data);
}

TEST_F(TpmUtilityTest, Unseal) {
  EXPECT_CALL(mock_tpm_utility_, UnsealData("fake_sealed", _, _))
      .WillOnce(DoAll(SetArgPointee<2>("fake_data"), Return(TPM_RC_SUCCESS)));
  std::string data;
  EXPECT_TRUE(tpm_utility_->Unseal("fake_sealed", &data));
  EXPECT_EQ("fake_data", data);
}

TEST_F(TpmUtilityTest, UnsealFail) {
  EXPECT_CALL(mock_tpm_utility_, UnsealData("fake_sealed", _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string data;
  EXPECT_FALSE(tpm_utility_->Unseal("fake_sealed", &data));
  EXPECT_EQ("", data);
}

TEST_F(TpmUtilityTest, GetEndorsementPublicKey) {
  std::string key;
  EXPECT_TRUE(tpm_utility_->GetEndorsementPublicKey(KEY_TYPE_RSA, &key));
  EXPECT_TRUE(tpm_utility_->GetEndorsementPublicKey(KEY_TYPE_ECC, &key));
}

TEST_F(TpmUtilityTest, GetEndorsementPublicKeyModulusSuccess) {
  EXPECT_CALL(mock_tpm_utility_, GetPublicRSAEndorsementKeyModulus(_))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  std::string key;
  EXPECT_TRUE(tpm_utility_->GetEndorsementPublicKeyModulus(
      KEY_TYPE_RSA, &key));
  // The key type ECC is not implemented yet.
  EXPECT_FALSE(tpm_utility_->GetEndorsementPublicKeyModulus(
      KEY_TYPE_ECC, &key));
}

TEST_F(TpmUtilityTest, GetEndorsementPublicKeyModulusNoKey) {
  EXPECT_CALL(mock_tpm_utility_, GetPublicRSAEndorsementKeyModulus(_))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string key;
  EXPECT_FALSE(tpm_utility_->GetEndorsementPublicKeyModulus(
      KEY_TYPE_RSA, &key));
  EXPECT_TRUE(key.empty());
  EXPECT_FALSE(tpm_utility_->GetEndorsementPublicKeyModulus(
      KEY_TYPE_ECC, &key));
  EXPECT_TRUE(key.empty());
}

TEST_F(TpmUtilityTest, GetEndorsementCertificate) {
  std::string certificate;
  EXPECT_TRUE(
      tpm_utility_->GetEndorsementCertificate(KEY_TYPE_RSA, &certificate));
  EXPECT_TRUE(last_read_space_request_.has_index());
  last_read_space_request_.Clear();
  EXPECT_TRUE(
      tpm_utility_->GetEndorsementCertificate(KEY_TYPE_ECC, &certificate));
  EXPECT_TRUE(last_read_space_request_.has_index());
}

TEST_F(TpmUtilityTest, GetEndorsementCertificateNoCert) {
  next_read_space_reply_.set_result(
      tpm_manager::NVRAM_RESULT_SPACE_DOES_NOT_EXIST);
  std::string certificate;
  EXPECT_FALSE(
      tpm_utility_->GetEndorsementCertificate(KEY_TYPE_RSA, &certificate));
  EXPECT_TRUE(last_read_space_request_.has_index());
  last_read_space_request_.Clear();
  EXPECT_FALSE(
      tpm_utility_->GetEndorsementCertificate(KEY_TYPE_ECC, &certificate));
  EXPECT_TRUE(last_read_space_request_.has_index());
}

TEST_F(TpmUtilityTest, Unbind) {
  EXPECT_CALL(mock_tpm_utility_,
              AsymmetricDecrypt(_, _, _, "fake_encrypted", _, _))
      .WillOnce(DoAll(SetArgPointee<5>("fake_data"), Return(TPM_RC_SUCCESS)));
  std::string data;
  EXPECT_TRUE(tpm_utility_->Unbind("fake_key_blob", "fake_encrypted", &data));
  EXPECT_EQ("fake_data", data);
}

TEST_F(TpmUtilityTest, UnbindFail) {
  EXPECT_CALL(mock_tpm_utility_,
              AsymmetricDecrypt(_, _, _, "fake_encrypted", _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string data;
  EXPECT_FALSE(tpm_utility_->Unbind("fake_key_blob", "fake_encrypted", &data));
  EXPECT_EQ("", data);
}

TEST_F(TpmUtilityTest, Sign) {
  EXPECT_CALL(mock_tpm_utility_, Sign(_, _, _, "fake_to_sign", true, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>("fake_signature"), Return(TPM_RC_SUCCESS)));
  std::string signature;
  EXPECT_TRUE(tpm_utility_->Sign("fake_key_blob", "fake_to_sign", &signature));
  EXPECT_EQ("fake_signature", signature);
}

TEST_F(TpmUtilityTest, SignFail) {
  EXPECT_CALL(mock_tpm_utility_, Sign(_, _, _, "fake_to_sign", true, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string signature;
  EXPECT_FALSE(tpm_utility_->Sign("fake_key_blob", "fake_to_sign", &signature));
  EXPECT_EQ("", signature);
}

TEST_F(TpmUtilityTest, CreateRestrictedKey) {
  EXPECT_CALL(mock_tpm_utility_, CreateIdentityKey(trunks::TPM_ALG_RSA, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>("fake_key_blob"), Return(TPM_RC_SUCCESS)));
  std::string expected_public_key;
  trunks::TPMT_PUBLIC public_area = GetValidPublicKey(&expected_public_key);
  EXPECT_CALL(mock_blob_parser_, ParseKeyBlob("fake_key_blob", _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(trunks::Make_TPM2B_PUBLIC(public_area)),
                Return(true)));
  std::string public_key;
  std::string private_key;
  EXPECT_TRUE(tpm_utility_->CreateRestrictedKey(KEY_TYPE_RSA, KEY_USAGE_SIGN,
                                                &public_key, &private_key));
  EXPECT_EQ(expected_public_key, public_key);
  EXPECT_EQ("fake_key_blob", private_key);
}

TEST_F(TpmUtilityTest, CreateRestrictedKeyFail) {
  EXPECT_CALL(mock_tpm_utility_, CreateIdentityKey(trunks::TPM_ALG_RSA, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string public_key;
  std::string private_key;
  EXPECT_FALSE(tpm_utility_->CreateRestrictedKey(KEY_TYPE_RSA, KEY_USAGE_SIGN,
                                                 &public_key, &private_key));
  EXPECT_EQ("", public_key);
  EXPECT_EQ("", private_key);
}

TEST_F(TpmUtilityTest, CreateRestrictedKeyParserFail) {
  EXPECT_CALL(mock_tpm_utility_, CreateIdentityKey(trunks::TPM_ALG_RSA, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>("fake_key_blob"), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_blob_parser_, ParseKeyBlob("fake_key_blob", _, _))
      .WillRepeatedly(Return(false));
  std::string public_key;
  std::string private_key;
  EXPECT_FALSE(tpm_utility_->CreateRestrictedKey(KEY_TYPE_RSA, KEY_USAGE_SIGN,
                                                 &public_key, &private_key));
  EXPECT_EQ("", public_key);
}

TEST_F(TpmUtilityTest, QuotePCR) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(5, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>("fake_pcr_value"), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_SIGNATURE fake_signature;
  fake_signature.sig_alg = trunks::TPM_ALG_RSASSA;
  fake_signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote");
  EXPECT_CALL(mock_tpm_, QuoteSync(_, _, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(trunks::Make_TPM2B_ATTEST("fake_quoted_data")),
                SetArgPointee<6>(fake_signature), Return(TPM_RC_SUCCESS)));
  std::string value;
  std::string quoted_data;
  std::string quote;
  EXPECT_TRUE(
      tpm_utility_->QuotePCR(5, "fake_key_blob", &value, &quoted_data, &quote));
  EXPECT_EQ("fake_pcr_value", value);
  EXPECT_EQ("fake_quoted_data", quoted_data);
  EXPECT_NE(std::string::npos, quote.find("fake_quote"));
}

TEST_F(TpmUtilityTest, QuotePCRFail) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(5, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>("fake_pcr_value"), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_SIGNATURE fake_signature;
  fake_signature.sig_alg = trunks::TPM_ALG_RSASSA;
  fake_signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote");
  EXPECT_CALL(mock_tpm_, QuoteSync(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string value;
  std::string quoted_data;
  std::string quote;
  EXPECT_FALSE(
      tpm_utility_->QuotePCR(5, "fake_key_blob", &value, &quoted_data, &quote));
  EXPECT_EQ("", quoted_data);
  EXPECT_EQ("", quote);
}

TEST_F(TpmUtilityTest, QuotePCRFailReadPCR) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(5, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  std::string value;
  std::string quoted_data;
  std::string quote;
  EXPECT_FALSE(
      tpm_utility_->QuotePCR(5, "fake_key_blob", &value, &quoted_data, &quote));
  EXPECT_EQ("", value);
}

TEST_F(TpmUtilityTest, ReadPCR) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(5, _))
      .WillOnce(
          DoAll(SetArgPointee<1>("fake_pcr_value"), Return(TPM_RC_SUCCESS)));
  std::string value;
  EXPECT_TRUE(tpm_utility_->ReadPCR(5, &value));
  EXPECT_EQ("fake_pcr_value", value);
}

TEST_F(TpmUtilityTest, ReadPCRFail) {
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(5, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  std::string value;
  EXPECT_FALSE(tpm_utility_->ReadPCR(5, &value));
  EXPECT_EQ("", value);
}

TEST_F(TpmUtilityTest, GetRSAPublicKeyFromTpmPublicKey) {
  std::string public_key;
  GetValidPublicKey(&public_key);
  std::string der;
  EXPECT_TRUE(tpm_utility_->GetRSAPublicKeyFromTpmPublicKey(public_key, &der));
}

TEST_F(TpmUtilityTest, GetRSAPublicKeyFromTpmPublicKeyBadKey) {
  std::string der;
  EXPECT_FALSE(tpm_utility_->GetRSAPublicKeyFromTpmPublicKey("bad_key", &der));
}

TEST_F(TpmUtilityTest, RemoveOwnerDependency) {
  EXPECT_TRUE(tpm_utility_->RemoveOwnerDependency());
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Attestation,
            last_remove_dependency_request_.owner_dependency());
  next_remove_dependency_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_utility_->RemoveOwnerDependency());
}

}  // namespace attestation

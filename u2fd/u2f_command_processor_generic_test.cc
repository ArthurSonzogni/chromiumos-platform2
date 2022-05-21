// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_command_processor_generic.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "u2fd/mock_user_state.h"
#include "u2fd/sign_manager/mock_sign_manager.h"
#include "u2fd/sign_manager/sign_manager.h"
#include "u2fd/u2f_command_processor.h"
#include "u2fd/util.h"

namespace u2f {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::MatchesRegex;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr char kCredentialSecretHex[65] = {[0 ... 63] = 'F', '\0'};
constexpr char kUserAccount[5] = "user";
constexpr char kWebAuthnSecretString[33] = {[0 ... 31] = '\x12', '\0'};
// Dummy RP id.
constexpr char kRpId[] = "example.com";
// Wrong RP id is used to test app id extension path.
constexpr char kWrongRpId[] = "wrong.com";

brillo::Blob HexArrayToBlob(const char* array) {
  brillo::Blob blob;
  CHECK(base::HexStringToBytes(array, &blob));
  return blob;
}

std::string ToString(const std::vector<uint8_t>& v) {
  return std::string(v.begin(), v.end());
}

brillo::SecureBlob GetWebAuthnSecret() {
  return brillo::SecureBlob(kWebAuthnSecretString);
}

std::vector<uint8_t> GetCredentialSecret() {
  return HexArrayToBlob(kCredentialSecretHex);
}

std::vector<uint8_t> GetRpIdHash() {
  return util::Sha256(std::string(kRpId));
}

std::vector<uint8_t> GetWrongRpIdHash() {
  return util::Sha256(std::string(kWrongRpId));
}

std::string GetKeyBlobString() {
  return std::string(256, '\x13');
}

std::vector<uint8_t> GetFakeKeyBlob() {
  return std::vector<uint8_t>(256, '\x14');
}

std::vector<uint8_t> GetFakeCredentialIdWithoutHash() {
  return util::ToVector(std::string(1, '\x01') + std::string(3, '\x00') +
                        std::string(48, 'C'));
}
std::vector<uint8_t> GetFakeCredentialIdHash() {
  return util::Sha256(GetFakeCredentialIdWithoutHash());
}
std::vector<uint8_t> GetFakeCredentialIdValidHash() {
  return util::ToVector(ToString(GetFakeCredentialIdWithoutHash()) +
                        ToString(GetFakeCredentialIdHash()));
}
std::vector<uint8_t> GetFakeCredentialIdInvalidHash() {
  return util::ToVector(ToString(GetFakeCredentialIdWithoutHash()) +
                        std::string(32, 'S'));
}

std::vector<uint8_t> GetPubKey() {
  return std::vector<uint8_t>(128, '\x31');
}

std::string GetHashToSign() {
  return std::string(32, 'H');
}

std::string GetSignature() {
  return std::string(128, 'S');
}

}  // namespace

class U2fCommandProcessorGenericTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto mock_sign_manager = std::make_unique<MockSignManager>();
    mock_sign_manager_ = mock_sign_manager.get();
    auto mock_cryptohome_proxy =
        std::make_unique<org::chromium::UserDataAuthInterfaceProxyMock>();
    mock_cryptohome_proxy_ = mock_cryptohome_proxy.get();
    processor_ = std::unique_ptr<U2fCommandProcessorGeneric>(
        new U2fCommandProcessorGeneric(&mock_user_state_,
                                       std::move(mock_cryptohome_proxy),
                                       std::move(mock_sign_manager)));
    ExpectNoGetWebAuthnSecret();
    ExpectGetUser();
  }

 protected:
  void ExpectNoGetWebAuthnSecret() {
    EXPECT_CALL(*mock_cryptohome_proxy_, GetWebAuthnSecret(_, _, _, _))
        .Times(0);
  }

  void ExpectGetWebAuthnSecret() {
    user_data_auth::GetWebAuthnSecretReply reply;
    reply.set_webauthn_secret(kWebAuthnSecretString);
    EXPECT_CALL(*mock_cryptohome_proxy_, GetWebAuthnSecret(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));
  }

  void ExpectGetWebAuthnSecretFail() {
    EXPECT_CALL(*mock_cryptohome_proxy_, GetWebAuthnSecret(_, _, _, _))
        .WillOnce(Return(false));
  }

  void ExpectGetUser() {
    EXPECT_CALL(mock_user_state_, GetUser())
        .WillRepeatedly(Return(kUserAccount));
  }

  void ExpectSignManagerReady() {
    EXPECT_CALL(*mock_sign_manager_, IsReady()).WillRepeatedly(Return(true));
  }

  MakeCredentialResponse::MakeCredentialStatus U2fGenerate(
      std::vector<uint8_t>* credential_id,
      CredentialPublicKey* credential_pubkey,
      std::vector<uint8_t>* credential_key_blob) {
    return processor_->U2fGenerate(
        GetRpIdHash(), GetCredentialSecret(), PresenceRequirement::kNone,
        /*uv_compatible=*/true, /*auth_time_secret_hash=*/nullptr,
        credential_id, credential_pubkey, credential_key_blob);
  }

  GetAssertionResponse::GetAssertionStatus U2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>* credential_key_blob,
      std::vector<uint8_t>* signature) {
    return processor_->U2fSign(rp_id_hash, hash_to_sign, credential_id,
                               GetCredentialSecret(), credential_key_blob,
                               PresenceRequirement::kNone, signature);
  }

  HasCredentialsResponse::HasCredentialsStatus U2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>* credential_key_blob) {
    return processor_->U2fSignCheckOnly(
        rp_id_hash, credential_id, GetCredentialSecret(), credential_key_blob);
  }

  StrictMock<MockUserState> mock_user_state_;
  org::chromium::UserDataAuthInterfaceProxyMock* mock_cryptohome_proxy_;
  MockSignManager* mock_sign_manager_;

 private:
  std::unique_ptr<U2fCommandProcessorGeneric> processor_;
};

namespace {

TEST_F(U2fCommandProcessorGenericTest, U2fGenerateNoWebAuthnSecret) {
  std::vector<uint8_t> cred_id, cred_key_blob;
  CredentialPublicKey cred_pubkey;
  ExpectGetWebAuthnSecretFail();
  EXPECT_EQ(U2fGenerate(&cred_id, &cred_pubkey, &cred_key_blob),
            MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGenericTest, U2fGenerateSignManagerNotReady) {
  std::vector<uint8_t> cred_id, cred_key_blob;
  CredentialPublicKey cred_pubkey;
  ExpectGetWebAuthnSecret();
  EXPECT_CALL(*mock_sign_manager_, IsReady()).WillOnce(Return(false));
  EXPECT_EQ(U2fGenerate(&cred_id, &cred_pubkey, &cred_key_blob),
            MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGenericTest, U2fGenerateSignManagerCreateKeyFailed) {
  std::vector<uint8_t> cred_id, cred_key_blob;
  CredentialPublicKey cred_pubkey;
  ExpectGetWebAuthnSecret();
  ExpectSignManagerReady();
  EXPECT_CALL(*mock_sign_manager_, IsReady()).WillOnce(Return(true));
  EXPECT_CALL(*mock_sign_manager_,
              CreateKey(KeyType::kRsa, GetWebAuthnSecret(), _, _))
      .WillOnce(Return(false));
  EXPECT_EQ(U2fGenerate(&cred_id, &cred_pubkey, &cred_key_blob),
            MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignNoCredentialKeyBlob) {
  std::vector<uint8_t> signature;
  EXPECT_EQ(U2fSign(GetRpIdHash(), util::ToVector(GetHashToSign()),
                    GetFakeCredentialIdValidHash(),
                    /*credential_key_blob=*/nullptr, &signature),
            GetAssertionResponse::INVALID_REQUEST);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignInvalidHash) {
  std::vector<uint8_t> signature;
  auto fake_key_blob = GetFakeKeyBlob();
  EXPECT_EQ(
      U2fSign(GetRpIdHash(), util::ToVector(GetHashToSign()),
              GetFakeCredentialIdInvalidHash(), &fake_key_blob, &signature),
      GetAssertionResponse::INVALID_REQUEST);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignInvalidHmac) {
  std::vector<uint8_t> signature;
  ExpectGetWebAuthnSecret();
  auto fake_key_blob = GetFakeKeyBlob();
  EXPECT_EQ(U2fSign(GetRpIdHash(), util::ToVector(GetHashToSign()),
                    GetFakeCredentialIdValidHash(), &fake_key_blob, &signature),
            GetAssertionResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignWrongRpIdHash) {
  std::vector<uint8_t> cred_id, cred_key_blob;
  CredentialPublicKey cred_pubkey;
  ExpectGetWebAuthnSecret();
  ExpectSignManagerReady();
  EXPECT_CALL(*mock_sign_manager_,
              CreateKey(KeyType::kRsa, GetWebAuthnSecret(), _, _))
      .WillOnce(DoAll(SetArgPointee<2>(GetKeyBlobString()),
                      SetArgPointee<3>(GetPubKey()), Return(true)));
  EXPECT_EQ(U2fGenerate(&cred_id, &cred_pubkey, &cred_key_blob),
            MakeCredentialResponse::SUCCESS);

  std::string expected_cred_pubkey_regex = std::string("(31){128}");
  EXPECT_THAT(base::HexEncode(cred_pubkey.cbor),
              MatchesRegex(expected_cred_pubkey_regex));

  // U2fSign with wrong rp id hash should fail.
  std::vector<uint8_t> signature;
  ExpectGetWebAuthnSecret();
  EXPECT_EQ(U2fSign(GetWrongRpIdHash(), util::ToVector(GetHashToSign()),
                    cred_id, &cred_key_blob, &signature),
            GetAssertionResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignCheckOnlyTooLongCredId) {
  std::vector<uint8_t> cred_id(GetFakeCredentialIdValidHash());
  cred_id.push_back('C');
  auto fake_key_blob = GetFakeKeyBlob();
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), cred_id, &fake_key_blob),
            HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

TEST_F(U2fCommandProcessorGenericTest, U2fSignCheckOnlyInvalidHash) {
  auto fake_key_blob = GetFakeKeyBlob();
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), GetFakeCredentialIdInvalidHash(),
                             &fake_key_blob),
            HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

TEST_F(U2fCommandProcessorGenericTest, U2fGenerateSignSuccess) {
  std::vector<uint8_t> cred_id, cred_key_blob;
  CredentialPublicKey cred_pubkey;
  ExpectGetWebAuthnSecret();
  ExpectSignManagerReady();
  EXPECT_CALL(*mock_sign_manager_,
              CreateKey(KeyType::kRsa, GetWebAuthnSecret(), _, _))
      .WillOnce(DoAll(SetArgPointee<2>(GetKeyBlobString()),
                      SetArgPointee<3>(GetPubKey()), Return(true)));
  EXPECT_EQ(U2fGenerate(&cred_id, &cred_pubkey, &cred_key_blob),
            MakeCredentialResponse::SUCCESS);

  std::string expected_cred_pubkey_regex = std::string("(31){128}");
  EXPECT_THAT(base::HexEncode(cred_pubkey.cbor),
              MatchesRegex(expected_cred_pubkey_regex));

  // U2fSignCheckOnly should succeed.
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), cred_id, &cred_key_blob),
            HasCredentialsResponse::SUCCESS);

  // U2fSign should succeed.
  std::vector<uint8_t> signature;
  ExpectGetWebAuthnSecret();
  EXPECT_CALL(*mock_sign_manager_,
              Sign(GetKeyBlobString(), GetHashToSign(), GetWebAuthnSecret(), _))
      .WillOnce(DoAll(SetArgPointee<3>(GetSignature()), Return(true)));
  EXPECT_EQ(U2fSign(GetRpIdHash(), util::ToVector(GetHashToSign()), cred_id,
                    &cred_key_blob, &signature),
            GetAssertionResponse::SUCCESS);
  EXPECT_EQ(signature, util::ToVector(GetSignature()));
}

}  // namespace

}  // namespace u2f

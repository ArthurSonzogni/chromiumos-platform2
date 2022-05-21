// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "u2fd/mock_tpm_vendor_cmd.h"
#include "u2fd/u2f_command_processor_gsc.h"
#include "u2fd/util.h"

namespace u2f {
namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Matcher;
using ::testing::MatchesRegex;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr base::TimeDelta kVerificationTimeout = base::Seconds(10);
constexpr base::TimeDelta kRequestPresenceDelay = base::Milliseconds(500);
constexpr int kMaxRetries = kVerificationTimeout / kRequestPresenceDelay;
constexpr uint32_t kCr50StatusSuccess = 0;
constexpr uint32_t kCr50StatusNotAllowed = 0x507;
constexpr uint32_t kCr50StatusPasswordRequired = 0x50a;

constexpr char kCredentialSecret[65] = {[0 ... 63] = 'E', '\0'};
// Dummy RP id.
constexpr char kRpId[] = "example.com";
// Wrong RP id is used to test app id extension path.
constexpr char kWrongRpId[] = "wrong.com";

std::vector<uint8_t> GetRpIdHash() {
  return util::Sha256(std::string(kRpId));
}

std::vector<uint8_t> GetWrongRpIdHash() {
  return util::Sha256(std::string(kWrongRpId));
}

std::vector<uint8_t> GetHashToSign() {
  return std::vector<uint8_t>(U2F_P256_SIZE, 0xcd);
}

std::vector<uint8_t> GetDataToSign() {
  return std::vector<uint8_t>(256, 0xcd);
}

std::vector<uint8_t> GetUserSecret() {
  return std::vector<uint8_t>(32, 'E');
}

std::vector<uint8_t> GetCredId() {
  return std::vector<uint8_t>(U2F_V0_KH_SIZE, 0xFD);
}

std::vector<uint8_t> GetVersionedCredId() {
  return std::vector<uint8_t>(U2F_V1_KH_SIZE + SHA256_DIGEST_LENGTH, 0xFD);
}

std::vector<uint8_t> GetAuthTimeSecretHash() {
  return std::vector<uint8_t>(32, 0xFD);
}

std::string ExpectedUserPresenceU2fGenerateRequestRegex(bool uv_compatible) {
  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex_uv_compatible =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("[A-F0-9]{64}") +  // Credential Secret
      std::string("0B") +            // U2F_UV_ENABLED_KH | U2F_AUTH_ENFORCE
      std::string("(FD){32}");       // Auth time secret hash

  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +  // Legacy user secret
      std::string("03") +        // U2F_UV_ENABLED_KH | U2F_AUTH_ENFORCE
      std::string("(00){32}");   // Auth time secret hash, unset
  return uv_compatible ? request_regex_uv_compatible : request_regex;
}

std::string ExpectedUserVerificationU2fGenerateRequestRegex() {
  // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("[A-F0-9]{64}") +  // Credential Secret
      std::string("08") +            // U2F_UV_ENABLED_KH
      std::string("(FD){32}");       // Auth time secret hash
  return request_regex;
}

// Only used to test U2fSign, where the hash to sign can be determined.
std::string ExpectedDeterministicU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +  // Credential Secret
      std::string("(FD){64}") +  // Key handle
      std::string("(CD){32}") +  // Hash to sign
      std::string("03");         // U2F_AUTH_ENFORCE
  return request_regex;
}

// User-verification flow version.
std::string ExpectedDeterministicU2fSignVersionedRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +  // User Secret
      std::string("(00){32}") +  // Auth time secret
      std::string("(CD){32}") +  // Hash to sign
      std::string("00") +        // Flag
      std::string("(FD){113}");  // Versioned Key handle
  return request_regex;
}

std::string ExpectedU2fSignCheckOnlyRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +  // User Secret
      std::string("(FD){64}") +  // Key handle
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07");         // U2F_AUTH_CHECK_ONLY
  return request_regex;
}

std::string ExpectedU2fSignCheckOnlyRequestRegexWrongRpId() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetWrongRpIdHash().data(),
                      GetWrongRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +                     // User Secret
      std::string("(FD){64}") +                     // Key handle
      std::string("(00){32}") +                     // Hash to sign (empty)
      std::string("07");                            // U2F_AUTH_CHECK_ONLY
  return request_regex;
}

// User-verification flow version
std::string ExpectedU2fSignCheckOnlyVersionedRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(GetRpIdHash().data(), GetRpIdHash().size()) +  // AppId
      std::string("(EE){32}") +  // User Secret
      std::string("(00){32}") +  // Auth time secret
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07") +        // U2F_AUTH_CHECK_ONLY
      std::string("(FD){113}");  // Versioned Key handle
  return request_regex;
}

// Dummy cr50 U2F_GENERATE_RESP.
const struct u2f_generate_resp kU2fGenerateResponse = {
    .pubKey = {.pointFormat = 0xAB,
               .x = {[0 ... 31] = 0xAB},
               .y = {[0 ... 31] = 0xAB}},
    .keyHandle = {.origin_seed = {[0 ... 31] = 0xFD},
                  .hmac = {[0 ... 31] = 0xFD}}};
const struct u2f_generate_versioned_resp kU2fGenerateVersionedResponse = {
    .pubKey = {.pointFormat = 0xAB,
               .x = {[0 ... 31] = 0xAB},
               .y = {[0 ... 31] = 0xAB}},
    .keyHandle = {.header = {.version = 0xFD,
                             .origin_seed = {[0 ... 31] = 0xFD},
                             .kh_hmac = {[0 ... 31] = 0xFD}},
                  .authorization_salt = {[0 ... 15] = 0xFD},
                  .authorization_hmac = {[0 ... 31] = 0xFD}}};

// Dummy cr50 U2F_SIGN_RESP.
const struct u2f_sign_resp kU2fSignResponse = {.sig_r = {[0 ... 31] = 0x12},
                                               .sig_s = {[0 ... 31] = 0x34}};

brillo::Blob HexArrayToBlob(const char* array) {
  brillo::Blob blob;
  CHECK(base::HexStringToBytes(array, &blob));
  return blob;
}

MATCHER_P(StructMatchesRegex, pattern, "") {
  std::string arg_hex = base::HexEncode(&arg, sizeof(arg));
  if (std::regex_match(arg_hex, std::regex(pattern))) {
    return true;
  }
  *result_listener << arg_hex << " did not match regex: " << pattern;
  return false;
}

}  // namespace

class U2fCommandProcessorGscTest : public ::testing::Test {
 public:
  void SetUp() override {
    processor_ =
        std::make_unique<U2fCommandProcessorGsc>(&mock_tpm_proxy_, [this]() {
          presence_requested_count_++;
          task_environment_.FastForwardBy(kRequestPresenceDelay);
        });
  }

  void TearDown() override {
    EXPECT_EQ(presence_requested_expected_, presence_requested_count_);
  }

 protected:
  static std::vector<uint8_t> GetCredPubKeyRaw() {
    return std::vector<uint8_t>(65, 0xAB);
  }

  static std::vector<uint8_t> GetCredPubKeyCbor() {
    return U2fCommandProcessorGsc::EncodeCredentialPublicKeyInCBOR(
        GetCredPubKeyRaw());
  }

  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status) {
    processor_->CallAndWaitForPresence(fn, status);
  }

  bool PresenceRequested() { return presence_requested_count_ > 0; }

  MakeCredentialResponse::MakeCredentialStatus U2fGenerate(
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      const brillo::Blob* auth_time_secret_hash,
      std::vector<uint8_t>* credential_id,
      CredentialPublicKey* credential_pubkey) {
    // U2fGenerate expects some output fields to be non-null, but we
    // want to support nullptr output fields in this helper method.
    std::vector<uint8_t> cred_id;
    CredentialPublicKey pubkey;
    if (!credential_id) {
      credential_id = &cred_id;
    }
    if (!credential_pubkey) {
      credential_pubkey = &pubkey;
    }
    return processor_->U2fGenerate(
        GetRpIdHash(), HexArrayToBlob(kCredentialSecret), presence_requirement,
        uv_compatible, auth_time_secret_hash, credential_id, credential_pubkey,
        nullptr);
  }

  GetAssertionResponse::GetAssertionStatus U2fSign(
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) {
    return processor_->U2fSign(GetRpIdHash(), hash_to_sign, credential_id,
                               HexArrayToBlob(kCredentialSecret), nullptr,
                               presence_requirement, signature);
  }

  HasCredentialsResponse::HasCredentialsStatus U2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id) {
    return processor_->U2fSignCheckOnly(
        rp_id_hash, credential_id, HexArrayToBlob(kCredentialSecret), nullptr);
  }

  MakeCredentialResponse::MakeCredentialStatus G2fAttest(
      const std::vector<uint8_t>& data,
      const brillo::SecureBlob& secret,
      std::vector<uint8_t>* signature_out) {
    return processor_->G2fAttest(data, secret, U2F_ATTEST_FORMAT_REG_RESP,
                                 signature_out);
  }

  void InsertAuthTimeSecretHashToCredentialId(std::vector<uint8_t>* input) {
    auto hash = GetAuthTimeSecretHash();
    processor_->InsertAuthTimeSecretHashToCredentialId(&hash, input);
  }

  int presence_requested_expected_ = 0;
  StrictMock<MockTpmVendorCommandProxy> mock_tpm_proxy_;

 private:
  int presence_requested_count_ = 0;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  std::unique_ptr<U2fCommandProcessorGsc> processor_;
};

namespace {

TEST_F(U2fCommandProcessorGscTest, CallAndWaitForPresenceDirectSuccess) {
  uint32_t status = kCr50StatusNotAllowed;
  // If presence is already available, we won't request it.
  CallAndWaitForPresence([]() { return kCr50StatusSuccess; }, &status);
  EXPECT_EQ(status, kCr50StatusSuccess);
  presence_requested_expected_ = 0;
}

TEST_F(U2fCommandProcessorGscTest, CallAndWaitForPresenceRequestSuccess) {
  uint32_t status = kCr50StatusNotAllowed;
  CallAndWaitForPresence(
      [this]() {
        if (PresenceRequested())
          return kCr50StatusSuccess;
        return kCr50StatusNotAllowed;
      },
      &status);
  EXPECT_EQ(status, kCr50StatusSuccess);
  presence_requested_expected_ = 1;
}

TEST_F(U2fCommandProcessorGscTest, CallAndWaitForPresenceTimeout) {
  uint32_t status = kCr50StatusSuccess;
  base::TimeTicks verification_start = base::TimeTicks::Now();
  CallAndWaitForPresence([]() { return kCr50StatusNotAllowed; }, &status);
  EXPECT_GE(base::TimeTicks::Now() - verification_start, kVerificationTimeout);
  EXPECT_EQ(status, kCr50StatusNotAllowed);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(U2fCommandProcessorGscTest, U2fGenerateVersionedNoAuthTimeSecretHash) {
  EXPECT_EQ(U2fGenerate(PresenceRequirement::kPowerButton,
                        /* uv_compatible = */ true, nullptr, nullptr, nullptr),
            MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(U2fCommandProcessorGscTest, U2fGenerateVersionedSuccessUserPresence) {
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex(true)),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id;
  CredentialPublicKey cred_pubkey;
  auto auth_time_secret_hash = GetAuthTimeSecretHash();
  EXPECT_EQ(U2fGenerate(PresenceRequirement::kPowerButton,
                        /* uv_compatible = */ true, &auth_time_secret_hash,
                        &cred_id, &cred_pubkey),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, GetVersionedCredId());
  EXPECT_EQ(cred_pubkey.cbor, U2fCommandProcessorGscTest::GetCredPubKeyCbor());
  EXPECT_EQ(cred_pubkey.raw, U2fCommandProcessorGscTest::GetCredPubKeyRaw());
  presence_requested_expected_ = 1;
}

TEST_F(U2fCommandProcessorGscTest, U2fGenerateVersionedNoUserPresence) {
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex(true)),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  auto auth_time_secret_hash = GetAuthTimeSecretHash();
  EXPECT_EQ(U2fGenerate(PresenceRequirement::kPowerButton,
                        /* uv_compatible = */ true, &auth_time_secret_hash,
                        nullptr, nullptr),
            MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(U2fCommandProcessorGscTest, U2fGenerateSuccessUserPresence) {
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(
                          ExpectedUserPresenceU2fGenerateRequestRegex(false)),
                      Matcher<u2f_generate_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id;
  CredentialPublicKey cred_pubkey;
  EXPECT_EQ(
      U2fGenerate(PresenceRequirement::kPowerButton,
                  /* uv_compatible = */ false, nullptr, &cred_id, &cred_pubkey),
      MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, GetCredId());
  EXPECT_EQ(cred_pubkey.cbor, U2fCommandProcessorGscTest::GetCredPubKeyCbor());
  EXPECT_EQ(cred_pubkey.raw, U2fCommandProcessorGscTest::GetCredPubKeyRaw());
  presence_requested_expected_ = 1;
}

TEST_F(U2fCommandProcessorGscTest, U2fGenerateNoUserPresence) {
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(StructMatchesRegex(
                          ExpectedUserPresenceU2fGenerateRequestRegex(false)),
                      Matcher<u2f_generate_resp*>(_)))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  EXPECT_EQ(U2fGenerate(PresenceRequirement::kPowerButton,
                        /* uv_compatible = */ false, nullptr, nullptr, nullptr),
            MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(U2fCommandProcessorGscTest,
       U2fGenerateVersionedSuccessUserVerification) {
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserVerificationU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      // Should succeed at the first time since no presence is required.
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id;
  CredentialPublicKey cred_pubkey;
  auto auth_time_secret_hash = GetAuthTimeSecretHash();
  // UI has verified the user so do not require presence.
  EXPECT_EQ(U2fGenerate(PresenceRequirement::kNone, /* uv_compatible = */ true,
                        &auth_time_secret_hash, &cred_id, &cred_pubkey),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, GetVersionedCredId());
  EXPECT_EQ(cred_pubkey.cbor, U2fCommandProcessorGscTest::GetCredPubKeyCbor());
  EXPECT_EQ(cred_pubkey.raw, U2fCommandProcessorGscTest::GetCredPubKeyRaw());
}

TEST_F(U2fCommandProcessorGscTest, U2fSignPresenceNoPresence) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  std::vector<uint8_t> signature;
  EXPECT_EQ(U2fSign(GetHashToSign(), GetCredId(),
                    PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(U2fCommandProcessorGscTest, U2fSignPresenceSuccess) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> signature;
  EXPECT_EQ(U2fSign(GetHashToSign(), GetCredId(),
                    PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(signature, util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                                 kU2fSignResponse.sig_s));
  presence_requested_expected_ = 1;
}

TEST_F(U2fCommandProcessorGscTest, U2fSignVersionedSuccess) {
  brillo::Blob credential_id(GetVersionedCredId());
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedDeterministicU2fSignVersionedRequestRegex())),
                  _))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> signature;
  EXPECT_EQ(U2fSign(GetHashToSign(), credential_id, PresenceRequirement::kNone,
                    &signature),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(signature, util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                                 kU2fSignResponse.sig_s));
}

TEST_F(U2fCommandProcessorGscTest, U2fSignCheckOnlyWrongRpIdHash) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));
  EXPECT_EQ(U2fSignCheckOnly(GetWrongRpIdHash(), GetCredId()),
            HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

TEST_F(U2fCommandProcessorGscTest, U2fSignCheckOnlySuccess) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), GetCredId()),
            HasCredentialsResponse::SUCCESS);
}

TEST_F(U2fCommandProcessorGscTest, U2fSignCheckOnlyVersionedSuccess) {
  brillo::Blob credential_id(GetVersionedCredId());
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedU2fSignCheckOnlyVersionedRequestRegex())),
                  _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), credential_id),
            HasCredentialsResponse::SUCCESS);
}

TEST_F(U2fCommandProcessorGscTest, U2fSignCheckOnlyWrongLength) {
  std::vector<uint8_t> wrong_length_key_handle(U2F_V0_KH_SIZE + 1, 0xab);
  EXPECT_EQ(U2fSignCheckOnly(GetRpIdHash(), wrong_length_key_handle),
            HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

TEST_F(U2fCommandProcessorGscTest, G2fAttestSuccess) {
  EXPECT_CALL(mock_tpm_proxy_, SendU2fAttest(_, _)).WillOnce(Return(0));
  auto secret = brillo::SecureBlob(GetUserSecret());
  brillo::Blob signature_out;
  EXPECT_EQ(G2fAttest(GetDataToSign(), secret, &signature_out),
            MakeCredentialResponse::SUCCESS);
}

TEST_F(U2fCommandProcessorGscTest, InsertAuthTimeSecretHashToCredentialId) {
  std::vector<uint8_t> input;
  input.reserve(sizeof(u2f_versioned_key_handle));
  input.insert(input.cend(), 65, 0x01);  // header
  input.insert(input.cend(), 16, 0x02);  // authorization_salt
  input.insert(input.cend(), 32, 0x03);  // authorization_hmac
  InsertAuthTimeSecretHashToCredentialId(&input);

  const std::string expected_output(
      "(01){65}"    // header
      "(02){16}"    // authorization_salt
      "(FD){32}"    // auth_time_secret_hash
      "(03){32}");  // authorization_hmac
  EXPECT_THAT(base::HexEncode(input.data(), input.size()),
              MatchesRegex(expected_output));
}

}  // namespace

}  // namespace u2f

// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
#include <chromeos/dbus/service_constants.h>
#include <cryptohome-client-test/cryptohome/dbus-proxy-mocks.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "u2fd/mock_allowlisting_util.h"
#include "u2fd/mock_tpm_vendor_cmd.h"
#include "u2fd/mock_user_state.h"
#include "u2fd/mock_webauthn_storage.h"
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
using ::testing::Unused;

constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;
constexpr int kMaxRetries =
    kVerificationTimeoutMs * 1000 / kVerificationRetryDelayUs;
constexpr uint32_t kCr50StatusSuccess = 0;
constexpr uint32_t kCr50StatusNotAllowed = 0x507;
constexpr uint32_t kCr50StatusPasswordRequired = 0x50a;

// Dummy User State.
constexpr char kUserSecret[65] = {[0 ... 63] = 'E', '\0'};
constexpr char kCredentialSecret[65] = {[0 ... 63] = 'E', '\0'};
// Dummy RP id.
constexpr char kRpId[] = "example.com";
// Wrong RP id is used to test app id extension path.
constexpr char kWrongRpId[] = "wrong.com";
const std::vector<uint8_t> kRpIdHash = util::Sha256(std::string(kRpId));
// Dummy key handle (credential ID).
const std::vector<uint8_t> kKeyHandle(sizeof(struct u2f_key_handle), 0xab);
// Dummy hash to sign.
const std::vector<uint8_t> kHashToSign(U2F_P256_SIZE, 0xcd);

// AAGUID for none attestation.
const std::vector<uint8_t> kAaguid = {0x84, 0x03, 0x98, 0x77, 0xa5, 0x4b,
                                      0xdf, 0xbb, 0x04, 0xa8, 0x2d, 0xf2,
                                      0xfa, 0x2a, 0x11, 0x6e};

// Only used to test DoU2fSign, where the hash to sign can be determined.
const std::string ExpectedDeterministicU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +  // Credential Secret
      std::string("(AB){64}") +  // Key handle
      std::string("(CD){32}") +  // Hash to sign
      std::string("03");         // U2F_AUTH_ENFORCE
  return request_regex;
}

const std::string ExpectedU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(AB){64}") +                              // Key handle
      // Hash_to_sign depends on signature counter which isn't deterministic
      std::string("[A-F0-9]{64}") +  // Hash to sign
      std::string("03");             // U2F_AUTH_ENFORCE
  return request_regex;
}

// User-verification flow version.
const std::string ExpectedUVU2fSignRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(00){32}") +                              // Auth time secret
      // Hash_to_sign depends on signature counter which isn't deterministic
      std::string("[A-F0-9]{64}") +  // Hash to sign
      std::string("00") +            // Flag
      std::string("(AB){113}");      // Versioned Key handle
  return request_regex;
}

const std::string ExpectedU2fSignCheckOnlyRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(AB){64}") +                              // Key handle
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07");         // U2F_AUTH_CHECK_ONLY
  return request_regex;
}

const std::string ExpectedU2fSignCheckOnlyRequestRegexWrongRpId() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  const std::vector<uint8_t> rp_id_hash = util::Sha256(std::string(kWrongRpId));
  static const std::string request_regex =
      base::HexEncode(rp_id_hash.data(), rp_id_hash.size()) +  // AppId
      std::string("(EE){32}") +                                // User Secret
      std::string("(AB){64}") +                                // Key handle
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07");         // U2F_AUTH_CHECK_ONLY
  return request_regex;
}

// User-verification flow version
const std::string ExpectedUVU2fSignCheckOnlyRequestRegex() {
  // See U2F_SIGN_REQ in //platform/ec/include/u2f.h
  static const std::string request_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
      std::string("(EE){32}") +                              // User Secret
      std::string("(00){32}") +                              // Auth time secret
      std::string("(00){32}") +  // Hash to sign (empty)
      std::string("07") +        // U2F_AUTH_CHECK_ONLY
      std::string("(AB){113}");  // Versioned Key handle
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

// AuthenticatorData field sizes, in bytes.
constexpr int kRpIdHashBytes = 32;
constexpr int kAuthenticatorDataFlagBytes = 1;
constexpr int kSignatureCounterBytes = 4;
constexpr int kAaguidBytes = 16;
constexpr int kCredentialIdLengthBytes = 2;

brillo::SecureBlob ArrayToSecureBlob(const char* array) {
  brillo::SecureBlob blob;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(array, &blob));
  return blob;
}

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

// The base test fixture tests behaviors seen by general consumers. It
// disallows presence-only mode, because U2F isn't offered to general
// consumers.
class WebAuthnHandlerTestBase : public ::testing::Test {
 public:
  void SetUp() override {
    PrepareMockBus();
    CreateHandler(U2fMode::kDisabled, nullptr);
    PrepareMockStorage();
    // We use per-credential secret instead of the old user secret.
    ExpectNoGetUserSecret();
  }

  void TearDown() override {
    if (presence_requested_expected_ == kMaxRetries) {
      // Due to clock and scheduling variances, the actual retries before
      // timeout could be one less.
      EXPECT_TRUE(presence_requested_count_ == kMaxRetries ||
                  presence_requested_count_ == kMaxRetries - 1);
    } else {
      EXPECT_EQ(presence_requested_expected_, presence_requested_count_);
    }
  }

 protected:
  void PrepareMockBus() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options);

    mock_auth_dialog_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), chromeos::kUserAuthenticationServiceName,
        dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));

    // Set an expectation so that the MockBus will return our mock proxy.
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    chromeos::kUserAuthenticationServiceName,
                    dbus::ObjectPath(chromeos::kUserAuthenticationServicePath)))
        .WillOnce(Return(mock_auth_dialog_proxy_.get()));
  }

  void CreateHandler(U2fMode u2f_mode,
                     std::unique_ptr<AllowlistingUtil> allowlisting_util) {
    handler_ = std::make_unique<WebAuthnHandler>();
    PrepareMockCryptohome();
    handler_->Initialize(
        mock_bus_.get(), &mock_tpm_proxy_, &mock_user_state_, u2f_mode,
        [this]() { presence_requested_count_++; }, std::move(allowlisting_util),
        &mock_metrics_);
  }

  void PrepareMockCryptohome() {
    auto mock_cryptohome_proxy =
        std::make_unique<org::chromium::CryptohomeInterfaceProxyMock>();
    mock_cryptohome_proxy_ = mock_cryptohome_proxy.get();
    handler_->SetCryptohomeInterfaceProxyForTesting(
        std::move(mock_cryptohome_proxy));
  }

  void PrepareMockStorage() {
    auto mock_storage = std::make_unique<MockWebAuthnStorage>();
    mock_webauthn_storage_ = mock_storage.get();
    handler_->SetWebAuthnStorageForTesting(std::move(mock_storage));
    mock_webauthn_storage_->set_allow_access(true);
  }

  const std::string ExpectedUserPresenceU2fGenerateRequestRegex() {
    // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
    static const std::string request_regex =
        base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
        std::string("[A-F0-9]{64}") +  // Credential Secret
        std::string("0B") +            // U2F_UV_ENABLED_KH | U2F_AUTH_ENFORCE
        std::string("(12){32}");       // Auth time secret hash
    return request_regex;
  }

  const std::string ExpectedUserVerificationU2fGenerateRequestRegex() {
    // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
    static const std::string request_regex =
        base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
        std::string("[A-F0-9]{64}") +  // Credential Secret
        std::string("08") +            // U2F_UV_ENABLED_KH
        std::string("(12){32}");       // Auth time secret hash
    return request_regex;
  }

  void ExpectUVFlowSuccess() {
    mock_auth_dialog_response_ = dbus::Response::CreateEmpty();
    dbus::Response* response = mock_auth_dialog_response_.get();
    dbus::MessageWriter writer(response);
    writer.AppendBool(true);
    EXPECT_CALL(*mock_auth_dialog_proxy_, DoCallMethod(_, _, _))
        .WillOnce(
            [response](Unused, Unused,
                       base::OnceCallback<void(dbus::Response*)>* callback) {
              std::move(*callback).Run(response);
            });
  }

  void ExpectNoGetUserSecret() {
    EXPECT_CALL(mock_user_state_, GetUserSecret()).Times(0);
  }

  void ExpectGetUserSecret() { ExpectGetUserSecretForTimes(1); }

  void ExpectGetUserSecretForTimes(int times) {
    EXPECT_CALL(mock_user_state_, GetUserSecret())
        .Times(times)
        .WillRepeatedly(Return(ArrayToSecureBlob(kUserSecret)));
  }

  void ExpectGetCounter() {
    static const std::vector<uint8_t> kSignatureCounter({42, 23, 42, 23});
    EXPECT_CALL(mock_user_state_, GetCounter())
        .WillOnce(Return(kSignatureCounter));
  }

  void ExpectIncrementCounter() {
    EXPECT_CALL(mock_user_state_, IncrementCounter()).WillOnce(Return(true));
  }

  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status) {
    handler_->CallAndWaitForPresence(fn, status);
  }

  bool PresenceRequested() { return presence_requested_count_ > 0; }

  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_pubkey) {
    return handler_->DoU2fGenerate(
        kRpIdHash, HexArrayToBlob(kCredentialSecret), presence_requirement,
        /* uv_compatible = */ true, credential_id, credential_pubkey);
  }

  GetAssertionResponse::GetAssertionStatus DoU2fSign(
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) {
    return handler_->DoU2fSign(kRpIdHash, hash_to_sign, credential_id,
                               HexArrayToBlob(kCredentialSecret),
                               presence_requirement, signature);
  }

  std::vector<uint8_t> MakeAuthenticatorData(
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_public_key,
      bool user_verified,
      bool include_attested_credential_data,
      bool is_u2f_authenticator_credential) {
    base::Optional<std::vector<uint8_t>> authenticator_data =
        handler_->MakeAuthenticatorData(
            kRpIdHash, credential_id, credential_public_key, user_verified,
            include_attested_credential_data, is_u2f_authenticator_credential);
    DCHECK(authenticator_data);
    return *authenticator_data;
  }

  // Set up an auth-time secret hash as if a user has logged in.
  void SetUpAuthTimeSecretHash() {
    handler_->auth_time_secret_hash_ = std::make_unique<brillo::Blob>(32, 0x12);
  }

  void InsertAuthTimeSecretHashToCredentialId(std::vector<uint8_t>* input) {
    handler_->InsertAuthTimeSecretHashToCredentialId(input);
  }

  StrictMock<MockTpmVendorCommandProxy> mock_tpm_proxy_;
  StrictMock<MockUserState> mock_user_state_;

  std::unique_ptr<WebAuthnHandler> handler_;
  MockWebAuthnStorage* mock_webauthn_storage_;

  int presence_requested_expected_ = 0;

 private:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_auth_dialog_proxy_;
  std::unique_ptr<dbus::Response> mock_auth_dialog_response_;
  org::chromium::CryptohomeInterfaceProxyMock* mock_cryptohome_proxy_;
  testing::NiceMock<MetricsLibraryMock> mock_metrics_;
  int presence_requested_count_ = 0;
};

namespace {

TEST_F(WebAuthnHandlerTestBase, CallAndWaitForPresenceDirectSuccess) {
  uint32_t status = kCr50StatusNotAllowed;
  // If presence is already available, we won't request it.
  CallAndWaitForPresence([]() { return kCr50StatusSuccess; }, &status);
  EXPECT_EQ(status, kCr50StatusSuccess);
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTestBase, CallAndWaitForPresenceRequestSuccess) {
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

TEST_F(WebAuthnHandlerTestBase, CallAndWaitForPresenceTimeout) {
  uint32_t status = kCr50StatusSuccess;
  base::TimeTicks verification_start = base::TimeTicks::Now();
  CallAndWaitForPresence([]() { return kCr50StatusNotAllowed; }, &status);
  EXPECT_GE(base::TimeTicks::Now() - verification_start,
            base::TimeDelta::FromMilliseconds(kVerificationTimeoutMs));
  EXPECT_EQ(status, kCr50StatusNotAllowed);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTestBase, DoU2fGenerateNoAuthTimeSecretHash) {
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::INTERNAL_ERROR);
}

TEST_F(WebAuthnHandlerTestBase, DoU2fGenerateSuccessUserPresence) {
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id, cred_pubkey;
  EXPECT_EQ(
      DoU2fGenerate(PresenceRequirement::kPowerButton, &cred_id, &cred_pubkey),
      MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, std::vector<uint8_t>(113, 0xFD));
  EXPECT_EQ(cred_pubkey, std::vector<uint8_t>(65, 0xAB));
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTestBase, DoU2fGenerateSuccessUserVerification) {
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserVerificationU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      // Should succeed at the first time since no presence is required.
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> cred_id, cred_pubkey;
  // UI has verified the user so do not require presence.
  EXPECT_EQ(DoU2fGenerate(PresenceRequirement::kNone, &cred_id, &cred_pubkey),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(cred_id, std::vector<uint8_t>(113, 0xFD));
  EXPECT_EQ(cred_pubkey, std::vector<uint8_t>(65, 0xAB));
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTestBase, DoU2fSignPresenceNoPresence) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));
  std::vector<uint8_t> signature;
  EXPECT_EQ(DoU2fSign(kHashToSign, kKeyHandle,
                      PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::VERIFICATION_FAILED);
  presence_requested_expected_ = kMaxRetries;
}

TEST_F(WebAuthnHandlerTestBase, DoU2fSignPresenceSuccess) {
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedDeterministicU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));
  std::vector<uint8_t> signature;
  EXPECT_EQ(DoU2fSign(kHashToSign, kKeyHandle,
                      PresenceRequirement::kPowerButton, &signature),
            MakeCredentialResponse::SUCCESS);
  EXPECT_EQ(signature, util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                                 kU2fSignResponse.sig_s));
  presence_requested_expected_ = 1;
}

TEST_F(WebAuthnHandlerTestBase, MakeCredentialUninitialized) {
  // Use an uninitialized WebAuthnHandler object.
  handler_.reset(new WebAuthnHandler());
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  MakeCredentialRequest request;
  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, MakeCredentialEmptyRpId) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  MakeCredentialRequest request;
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, MakeCredentialNoAuthTimeSecretHash) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectUVFlowSuccess();

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  handler_->MakeCredential(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, MakeCredentialUPUpgradedToUV) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  // Thought it's going to be UV, we will still check if any exclude credential
  // matches legacy credentials.
  ExpectGetUserSecret();
  ExpectUVFlowSuccess();
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserVerificationU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTestBase, MakeCredentialVerificationSuccess) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  // Thought it's going to be UV, we will still check if any exclude credential
  // matches legacy credentials.
  ExpectGetUserSecret();
  ExpectUVFlowSuccess();

  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserVerificationU2fGenerateRequestRegex()),
          Matcher<u2f_generate_versioned_resp*>(_)))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateVersionedResponse),
                      Return(kCr50StatusSuccess)));
  // TODO(yichengli): Specify the parameter to WriteRecord.
  EXPECT_CALL(*mock_webauthn_storage_, WriteRecord(_)).WillOnce(Return(true));

  const std::string expected_authenticator_data_regex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // RP ID hash
      std::string(
          "45"          // Flag: user present, user verified, attested
                        // credential data included.
          "(..){4}") +  // Signature counter
      base::HexEncode(kAaguid.data(), kAaguid.size()) +  // AAGUID
      std::string(
          "0091"        // Credential ID length
                        // Credential ID, from kU2fGenerateVersionedResponse:
          "(FD){65}"    // Versioned key handle header
          "(FD){16}"    // Authorization salt
          "(12){32}"    // Hash of authorization secret
          "(FD){32}"    // Authorization hmac
                        // CBOR encoded credential public key:
          "A5"          // Start a CBOR map of 5 elements
          "01"          // unsigned(1), COSE key type field
          "02"          // unsigned(2), COSE key type EC2
          "03"          // unsigned(3), COSE key algorithm field
          "26"          // negative(6) = -7, COSE key algorithm ES256
          "20"          // negative(0) = -1, COSE EC key curve field
          "01"          // unsigned(1), COSE EC key curve
          "21"          // negative(1) = -2, COSE EC key x coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}"    // x coordinate, from kU2fGenerateVersionedResponse
          "22"          // negative(2) = -3, COSE EC key y coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}");  // y coordinate, from kU2fGenerateVersionedResponse

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_authenticator_data,
         const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::SUCCESS);
        EXPECT_THAT(base::HexEncode(resp.authenticator_data().data(),
                                    resp.authenticator_data().size()),
                    MatchesRegex(expected_authenticator_data));
        EXPECT_EQ(resp.attestation_format(), "none");
        EXPECT_EQ(resp.attestation_statement(), "\xa0");
        *called_ptr = true;
      },
      &called, expected_authenticator_data_regex));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, GetAssertionUninitialized) {
  // Use an uninitialized WebAuthnHandler object.
  handler_.reset(new WebAuthnHandler());
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INTERNAL_ERROR);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, GetAssertionEmptyRpId) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, GetAssertionWrongClientDataHashLength) {
  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::INVALID_REQUEST);
        *called_ptr = true;
      },
      &called));

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH - 1, 0xcd));
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

// Simulates the case where the KH doesn't match any record in daemon-store, or
// any legacy credential id.
TEST_F(WebAuthnHandlerTestBase, GetAssertionNoCredentialSecret) {
  GetAssertionRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kWrongRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(base::nullopt));
  ExpectGetUserSecret();

  // We will check for legacy credentials, so two check-only calls to TPM.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .Times(2)
      .WillRepeatedly(Return(kCr50StatusPasswordRequired));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

// Simulates the case where the KH matches a record in daemon-store but is not
// recognized by cr50. This is not very likely in reality unless daemon-store
// is compromised.
TEST_F(WebAuthnHandlerTestBase, GetAssertionInvalidKeyHandle) {
  GetAssertionRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kWrongRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(HexArrayToBlob(kCredentialSecret)));
  ExpectGetUserSecret();
  // 3 calls to TPM, one for each credential type.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .Times(3)
      .WillRepeatedly(Return(kCr50StatusPasswordRequired));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, GetAssertionUPUpgradedToUV) {
  // Needed for "InsertAuthTimeSecretHash" workaround.
  SetUpAuthTimeSecretHash();

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));

  std::vector<uint8_t> credential_id_vec(
      sizeof(struct u2f_versioned_key_handle), 0xab);
  InsertAuthTimeSecretHashToCredentialId(&credential_id_vec);
  const std::string credential_id(credential_id_vec.begin(),
                                  credential_id_vec.end());
  request.add_allowed_credential_id(credential_id);

  request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  // Pass DoU2fSignCheckOnly so that we can get to UV flow.
  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(HexArrayToBlob(kCredentialSecret)));
  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedUVU2fSignCheckOnlyRequestRegex())),
                  _))
      .WillRepeatedly(Return(kCr50StatusSuccess));
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(
                      StructMatchesRegex(ExpectedUVU2fSignRequestRegex())),
                  _));

  ExpectUVFlowSuccess();

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
}

TEST_F(WebAuthnHandlerTestBase, GetAssertionVerificationSuccess) {
  // Needed for "InsertAuthTimeSecretHash" workaround.
  SetUpAuthTimeSecretHash();

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));

  std::vector<uint8_t> credential_id_vec(
      sizeof(struct u2f_versioned_key_handle), 0xab);
  InsertAuthTimeSecretHashToCredentialId(&credential_id_vec);
  const std::string credential_id(credential_id_vec.begin(),
                                  credential_id_vec.end());
  request.add_allowed_credential_id(credential_id);

  request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  ExpectUVFlowSuccess();

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(HexArrayToBlob(kCredentialSecret)));
  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedUVU2fSignCheckOnlyRequestRegex())),
                  _))
      .WillRepeatedly(Return(kCr50StatusSuccess));
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(
                      StructMatchesRegex(ExpectedUVU2fSignRequestRegex())),
                  _))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_credential_id,
         const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(), expected_credential_id);
        EXPECT_THAT(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            MatchesRegex(base::HexEncode(kRpIdHash.data(),
                                         kRpIdHash.size()) +  // RP ID hash
                         std::string("05"  // Flag: user present, user verified
                                     "(..){4}")));  // Signature counter
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      &called, credential_id));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestBase, HasCredentialsNoMatch) {
  HasCredentialsRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kWrongRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(base::nullopt));
  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .Times(2)
      .WillRepeatedly(Return(kCr50StatusPasswordRequired));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 0);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

// Match first of the 3 types of credentials.
TEST_F(WebAuthnHandlerTestBase, HasCredentialsMatchPlatformAuthenticator) {
  HasCredentialsRequest request;
  request.set_rp_id(kRpId);
  request.set_app_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(HexArrayToBlob(kCredentialSecret)));
  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      // Checking for platform authenticator credential succeeds.
      .WillOnce(Return(kCr50StatusSuccess))
      // Checking for legacy credentials fails.
      .WillRepeatedly(Return(kCr50StatusPasswordRequired));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::SUCCESS);
}

// Match second of the 3 types of credentials.
TEST_F(WebAuthnHandlerTestBase, HasCredentialsMatchU2fhidWebAuthn) {
  HasCredentialsRequest request;
  request.set_rp_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(base::nullopt));
  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::SUCCESS);
}

// Match third of the 3 types of credentials.
TEST_F(WebAuthnHandlerTestBase, HasCredentialsMatchAppId) {
  HasCredentialsRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillOnce(Return(base::nullopt));
  ExpectGetUserSecret();
  // Matching rp_id fails.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));
  // Matching app_id succeeds.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));

  auto resp = handler_->HasCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::SUCCESS);
}

TEST_F(WebAuthnHandlerTestBase, HasLegacyCredentialsNoMatch) {
  HasCredentialsRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kWrongRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .Times(2)
      .WillRepeatedly(Return(kCr50StatusPasswordRequired));

  auto resp = handler_->HasLegacyCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 0);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
}

// Match second of the 3 types of credentials.
// If rp_id matches, it's a legacy credential registered with u2fhid on WebAuthn
// API.
TEST_F(WebAuthnHandlerTestBase, HasLegacyCredentialsMatchU2fhidWebAuthn) {
  HasCredentialsRequest request;
  request.set_rp_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));

  auto resp = handler_->HasLegacyCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::SUCCESS);
}

// Match third of the 3 types of credentials.
// If app_id matches, it's a legacy credential registered with U2F API.
TEST_F(WebAuthnHandlerTestBase, HasLegacyCredentialsMatchAppId) {
  HasCredentialsRequest request;
  request.set_rp_id(kWrongRpId);
  request.set_app_id(kRpId);
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_credential_id(credential_id);

  ExpectGetUserSecret();
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));

  auto resp = handler_->HasLegacyCredentials(request);
  EXPECT_EQ(resp.credential_id_size(), 1);
  EXPECT_EQ(resp.status(), HasCredentialsResponse::SUCCESS);
}

TEST_F(WebAuthnHandlerTestBase, MakeAuthenticatorDataWithAttestedCredData) {
  const std::vector<uint8_t> cred_id(64, 0xAA);
  const std::vector<uint8_t> cred_pubkey(65, 0xBB);

  std::vector<uint8_t> authenticator_data =
      MakeAuthenticatorData(cred_id, cred_pubkey, /* user_verified = */ false,
                            /* include_attested_credential_data = */ true,
                            /* is_u2f_authenticator_credential = */ false);
  EXPECT_EQ(authenticator_data.size(),
            kRpIdHashBytes + kAuthenticatorDataFlagBytes +
                kSignatureCounterBytes + kAaguidBytes +
                kCredentialIdLengthBytes + cred_id.size() + cred_pubkey.size());

  const std::string rp_id_hash_hex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size());
  const std::string expected_authenticator_data_regex =
      rp_id_hash_hex +  // RP ID hash
      std::string(
          "41"          // Flag: user present, attested credential data included
          "(..){4}") +  // Signature counter
      base::HexEncode(kAaguid.data(), kAaguid.size()) +  // AAGUID
      std::string(
          "0040"        // Credential ID length
          "(AA){64}"    // Credential ID
          "(BB){65}");  // Credential public key
  EXPECT_THAT(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      MatchesRegex(expected_authenticator_data_regex));
}

TEST_F(WebAuthnHandlerTestBase, MakeAuthenticatorDataNoAttestedCredData) {
  std::vector<uint8_t> authenticator_data =
      MakeAuthenticatorData(std::vector<uint8_t>(), std::vector<uint8_t>(),
                            /* user_verified = */ false,
                            /* include_attested_credential_data = */ false,
                            /* is_u2f_authenticator_credential = */ false);
  EXPECT_EQ(
      authenticator_data.size(),
      kRpIdHashBytes + kAuthenticatorDataFlagBytes + kSignatureCounterBytes);

  const std::string rp_id_hash_hex =
      base::HexEncode(kRpIdHash.data(), kRpIdHash.size());
  const std::string expected_authenticator_data_regex =
      rp_id_hash_hex +  // RP ID hash
      std::string(
          "01"         // Flag: user present
          "(..){4}");  // Signature counter
  EXPECT_THAT(
      base::HexEncode(authenticator_data.data(), authenticator_data.size()),
      MatchesRegex(expected_authenticator_data_regex));
}

TEST_F(WebAuthnHandlerTestBase,
       MakeAuthenticatorDataU2fAuthenticatorCredential) {
  // For U2F authenticator credentials only, the counter comes from UserState.
  ExpectGetCounter();
  ExpectIncrementCounter();

  std::vector<uint8_t> authenticator_data =
      MakeAuthenticatorData(std::vector<uint8_t>(), std::vector<uint8_t>(),
                            /* user_verified = */ false,
                            /* include_attested_credential_data = */ false,
                            /* is_u2f_authenticator_credential = */ true);

  EXPECT_EQ(
      base::HexEncode(authenticator_data),
      base::HexEncode(kRpIdHash) +
          std::string("01"           // Flag: user present
                      "2A172A17"));  // kSignatureCounter in network byte order
}

TEST_F(WebAuthnHandlerTestBase, InsertAuthTimeSecretHashToCredentialId) {
  SetUpAuthTimeSecretHash();
  std::vector<uint8_t> input;
  input.reserve(sizeof(u2f_versioned_key_handle));
  input.insert(input.cend(), 65, 0x01);  // header
  input.insert(input.cend(), 16, 0x02);  // authorization_salt
  input.insert(input.cend(), 32, 0x03);  // authorization_hmac
  InsertAuthTimeSecretHashToCredentialId(&input);

  const std::string expected_output(
      "(01){65}"    // header
      "(02){16}"    // authorization_salt
      "(12){32}"    // auth_time_secret_hash
      "(03){32}");  // authorization_hmac
  EXPECT_THAT(base::HexEncode(input.data(), input.size()),
              MatchesRegex(expected_output));
}

}  // namespace

// This test fixture tests the behavior when u2f is enabled on the device.
class WebAuthnHandlerTestU2fMode : public WebAuthnHandlerTestBase {
 public:
  void SetUp() override {
    PrepareMockBus();
    CreateHandler(U2fMode::kU2f, nullptr);
    PrepareMockStorage();
  }

 protected:
  const std::string ExpectedUserPresenceU2fGenerateRequestRegex() {
    // See U2F_GENERATE_REQ in //platform/ec/include/u2f.h
    static const std::string request_regex =
        base::HexEncode(kRpIdHash.data(), kRpIdHash.size()) +  // AppId
        std::string("(EE){32}") +  // Legacy user secret
        std::string("03") +        // U2F_UV_ENABLED_KH | U2F_AUTH_ENFORCE
        std::string("(00){32}");   // Auth time secret hash, unset
    return request_regex;
  }
};

namespace {

TEST_F(WebAuthnHandlerTestU2fMode, MakeCredentialPresenceSuccess) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetCounter();
  ExpectIncrementCounter();

  // 1. LegacyCredential uses "user secret" instead of per credential secret.
  // 2. We will still check if any exclude credential matches legacy
  // credentials.
  ExpectGetUserSecretForTimes(2);
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex()),
          Matcher<u2f_generate_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateResponse),
                      Return(kCr50StatusSuccess)));
  // Since this creates a legacy credential with legacy secret, we won't write
  // to storage.
  EXPECT_CALL(*mock_webauthn_storage_, WriteRecord(_)).Times(0);

  const std::string expected_authenticator_data_regex =
      base::HexEncode(kRpIdHash) +
      std::string(
          "41"          // Flag: user present, attested credential data included
          "2A172A17"    // kSignatureCounter in network byte order
          "(00){16}"    // AAGUID
          "0040"        // Credential ID length
                        // Credential ID, from kU2fGenerateResponse:
          "(FD){64}"    // (non-versioned) key handle
                        // CBOR encoded credential public key:
          "A5"          // Start a CBOR map of 5 elements
          "01"          // unsigned(1), COSE key type field
          "02"          // unsigned(2), COSE key type EC2
          "03"          // unsigned(3), COSE key algorithm field
          "26"          // negative(6) = -7, COSE key algorithm ES256
          "20"          // negative(0) = -1, COSE EC key curve field
          "01"          // unsigned(1), COSE EC key curve
          "21"          // negative(1) = -2, COSE EC key x coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}"    // x coordinate, from kU2fGenerateResponse
          "22"          // negative(2) = -3, COSE EC key y coordinate field
          "5820"        // Start a CBOR array of 32 bytes
          "(AB){32}");  // y coordinate, from kU2fGenerateResponse

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_authenticator_data,
         const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::SUCCESS);
        EXPECT_THAT(base::HexEncode(resp.authenticator_data().data(),
                                    resp.authenticator_data().size()),
                    MatchesRegex(expected_authenticator_data));
        EXPECT_EQ(resp.attestation_format(), "fido-u2f");
        const std::string expected_attestation_statement =
            "A2"      // Start a CBOR map of 2 elements
            "63"      // Start CBOR text of 3 chars
            "736967"  // "sig"
            ".+"      // Random signature
            "63"      // Start CBOR text of 3 chars
            "783563"  // "x5c"
            "81"      // Start CBOR array of 1 element
            ".+";     // Random x509
        EXPECT_THAT(base::HexEncode(resp.attestation_statement().data(),
                                    resp.attestation_statement().size()),
                    MatchesRegex(expected_attestation_statement));
        *called_ptr = true;
      },
      &called, expected_authenticator_data_regex));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestU2fMode, GetAssertionSignLegacyCredentialNoPresence) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetCounter();
  ExpectIncrementCounter();

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .Times(2)
      .WillRepeatedly(Return(base::nullopt));
  // LegacyCredential uses "user secret" instead of per credential secret.
  ExpectGetUserSecretForTimes(2);
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignRequestRegex())),
                          _))
      .WillRepeatedly(Return(kCr50StatusNotAllowed));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::VERIFICATION_FAILED);
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = kMaxRetries;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestU2fMode, GetAssertionSignLegacyCredentialSuccess) {
  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetCounter();
  ExpectIncrementCounter();

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .Times(2)
      .WillRepeatedly(Return(base::nullopt));
  // LegacyCredential uses "user secret" instead of per credential secret.
  ExpectGetUserSecretForTimes(2);
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(),
                  std::string(sizeof(struct u2f_key_handle), 0xab));
        EXPECT_EQ(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            base::HexEncode(kRpIdHash) +
                std::string(
                    "01"           // Flag: user present
                    "2A172A17"));  // kSignatureCounter in network byte order
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestU2fMode, GetAssertionSignLegacyCredentialAppIdMatch) {
  GetAssertionRequest request;
  request.set_rp_id(kWrongRpId);
  // Legacy credentials registered via U2F interface use the app id.
  request.set_app_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));
  const std::string credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(credential_id);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);

  ExpectGetCounter();
  ExpectIncrementCounter();

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .Times(2)
      .WillRepeatedly(Return(base::nullopt));
  // LegacyCredential uses "user secret" instead of per credential secret.
  ExpectGetUserSecretForTimes(2);

  // Rp id doesn't match.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegexWrongRpId())),
                          _))
      .WillOnce(Return(kCr50StatusPasswordRequired));
  // App id matches.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(),
                  std::string(sizeof(struct u2f_key_handle), 0xab));
        EXPECT_EQ(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            base::HexEncode(kRpIdHash) +
                std::string(
                    "01"           // Flag: user present
                    "2A172A17"));  // kSignatureCounter in network byte order
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      &called));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestU2fMode,
       GetAssertionSignVersionedCredentialInUVMode) {
  // Needed for "InsertAuthTimeSecretHash" workaround.
  SetUpAuthTimeSecretHash();

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));

  std::vector<uint8_t> credential_id_vec(
      sizeof(struct u2f_versioned_key_handle), 0xab);
  InsertAuthTimeSecretHashToCredentialId(&credential_id_vec);
  const std::string credential_id(credential_id_vec.begin(),
                                  credential_id_vec.end());
  request.add_allowed_credential_id(credential_id);

  request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  ExpectUVFlowSuccess();

  EXPECT_CALL(*mock_webauthn_storage_, GetSecretByCredentialId(credential_id))
      .WillRepeatedly(Return(HexArrayToBlob(kCredentialSecret)));
  ExpectGetUserSecret();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedUVU2fSignCheckOnlyRequestRegex())),
                  _))
      .WillRepeatedly(Return(kCr50StatusSuccess));
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(
                      StructMatchesRegex(ExpectedUVU2fSignRequestRegex())),
                  _))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_credential_id,
         const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(), expected_credential_id);
        EXPECT_THAT(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            MatchesRegex(base::HexEncode(kRpIdHash.data(),
                                         kRpIdHash.size()) +  // RP ID hash
                         std::string("05"  // Flag: user present, user verified
                                     "(..){4}")));  // Signature counter
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      &called, credential_id));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
  ASSERT_TRUE(called);
}

TEST_F(WebAuthnHandlerTestU2fMode,
       GetAssertionWithTwoTypesOfAllowedCredentials) {
  // Needed for "InsertAuthTimeSecretHash" workaround.
  SetUpAuthTimeSecretHash();

  GetAssertionRequest request;
  request.set_rp_id(kRpId);
  request.set_client_data_hash(std::string(SHA256_DIGEST_LENGTH, 0xcd));

  // Add a U2F credential to the allow list first.
  const std::string u2f_credential_id(sizeof(struct u2f_key_handle), 0xab);
  request.add_allowed_credential_id(u2f_credential_id);
  // Add a platform credential (second type).
  std::vector<uint8_t> platform_credential_id_vec(
      sizeof(struct u2f_versioned_key_handle), 0xab);
  InsertAuthTimeSecretHashToCredentialId(&platform_credential_id_vec);
  const std::string platform_credential_id(platform_credential_id_vec.begin(),
                                           platform_credential_id_vec.end());
  request.add_allowed_credential_id(platform_credential_id);

  request.set_verification_type(
      VerificationType::VERIFICATION_USER_VERIFICATION);

  ExpectUVFlowSuccess();

  EXPECT_CALL(*mock_webauthn_storage_,
              GetSecretByCredentialId(platform_credential_id))
      .WillRepeatedly(Return(HexArrayToBlob(kCredentialSecret)));
  EXPECT_CALL(*mock_webauthn_storage_,
              GetSecretByCredentialId(u2f_credential_id))
      .WillRepeatedly(Return(base::nullopt));
  ExpectGetUserSecret();
  // Both credentials should pass DoU2fSignCheckOnly, but only the platform
  // credential should go through DoU2fSign.
  EXPECT_CALL(mock_tpm_proxy_,
              SendU2fSign(Matcher<const u2f_sign_req&>(StructMatchesRegex(
                              ExpectedU2fSignCheckOnlyRequestRegex())),
                          _))
      .WillOnce(Return(kCr50StatusSuccess));
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(StructMatchesRegex(
                      ExpectedUVU2fSignCheckOnlyRequestRegex())),
                  _))
      .WillRepeatedly(Return(kCr50StatusSuccess));
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fSign(Matcher<const u2f_sign_versioned_req&>(
                      StructMatchesRegex(ExpectedUVU2fSignRequestRegex())),
                  _))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fSignResponse),
                      Return(kCr50StatusSuccess)));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<GetAssertionResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const std::string& expected_credential_id,
         const GetAssertionResponse& resp) {
        EXPECT_EQ(resp.status(), GetAssertionResponse::SUCCESS);
        ASSERT_EQ(resp.assertion_size(), 1);
        auto assertion = resp.assertion(0);
        EXPECT_EQ(assertion.credential_id(), expected_credential_id);
        EXPECT_THAT(
            base::HexEncode(assertion.authenticator_data().data(),
                            assertion.authenticator_data().size()),
            MatchesRegex(base::HexEncode(kRpIdHash.data(),
                                         kRpIdHash.size()) +  // RP ID hash
                         std::string("05"  // Flag: user present, user verified
                                     "(..){4}")));  // Signature counter
        EXPECT_EQ(util::ToVector(assertion.signature()),
                  util::SignatureToDerBytes(kU2fSignResponse.sig_r,
                                            kU2fSignResponse.sig_s));
        *called_ptr = true;
      },
      // The platform credential should appear in the assertion even though it
      // comes second in the allowed credential list.
      &called, platform_credential_id));

  handler_->GetAssertion(std::move(mock_method_response), request);
  presence_requested_expected_ = 0;
  ASSERT_TRUE(called);
}

}  // namespace

// This test fixture tests the behavior when g2f is enabled on the device.
class WebAuthnHandlerTestG2fMode : public WebAuthnHandlerTestU2fMode {
 public:
  void SetUp() override {
    PrepareMockBus();
    mock_allowlisting_util_ = new StrictMock<MockAllowlistingUtil>();
    CreateHandler(U2fMode::kU2fExtended,
                  std::unique_ptr<AllowlistingUtil>(mock_allowlisting_util_));
    PrepareMockStorage();
  }

 protected:
  StrictMock<MockAllowlistingUtil>* mock_allowlisting_util_;  // Not Owned.
};

namespace {

// Example of a cert that would be returned by cr50.
constexpr char kDummyG2fCert[] =
    "308201363081DDA0030201020210442D32429223D041240350303716EE6B300A06082A8648"
    "CE3D040302300F310D300B06035504031304637235303022180F3230303030313031303030"
    "3030305A180F32303939313233313233353935395A300F310D300B06035504031304637235"
    "303059301306072A8648CE3D020106082A8648CE3D030107034200045165719A9975F6FD30"
    "CC2516C22FE841F65F9D2EE7B8B72F76807AEBD8CA3376005C7FA86453E4B10DB7BFAD5D2B"
    "D00DB4A7C4845AD06D686ACD0252387618ECA31730153013060B2B0601040182E51C020101"
    "040403020308300A06082A8648CE3D0403020348003045022100F09976F373920FEF8205C4"
    "B1FB1DA21EB9F3F176B7DF433A1ADE0F3F38B721960220179D9B9051BFCCCC90BA6BB42B86"
    "111D7A9C4FB56DFD39FB426081DD027AD609";

std::string GetDummyG2fCert() {
  std::vector<uint8_t> cert;
  base::HexStringToBytes(kDummyG2fCert, &cert);
  return std::string(cert.begin(), cert.end());
}

TEST_F(WebAuthnHandlerTestG2fMode, MakeCredentialPresenceSuccess) {
  MakeCredentialRequest request;
  request.set_rp_id(kRpId);
  request.set_verification_type(VerificationType::VERIFICATION_USER_PRESENCE);
  request.set_attestation_conveyance_preference(MakeCredentialRequest::G2F);

  ExpectGetCounter();
  ExpectIncrementCounter();

  // We will need user secret 3 times:
  // first time for u2f_generate (legacy credential),
  // second time for g2f attestation command,
  // third time for checking if any exclude credential matches legacy
  // credentials.
  ExpectGetUserSecretForTimes(3);
  SetUpAuthTimeSecretHash();
  EXPECT_CALL(
      mock_tpm_proxy_,
      SendU2fGenerate(
          StructMatchesRegex(ExpectedUserPresenceU2fGenerateRequestRegex()),
          Matcher<u2f_generate_resp*>(_)))
      .WillOnce(Return(kCr50StatusNotAllowed))
      .WillOnce(DoAll(SetArgPointee<1>(kU2fGenerateResponse),
                      Return(kCr50StatusSuccess)));
  // Since this creates a legacy credential with legacy secret, we won't write
  // to storage.
  EXPECT_CALL(*mock_webauthn_storage_, WriteRecord(_)).Times(0);

  // G2f attestation mock.
  EXPECT_CALL(mock_tpm_proxy_, GetG2fCertificate(_))
      .WillOnce(DoAll(SetArgPointee<0>(GetDummyG2fCert()), Return(0)));
  EXPECT_CALL(mock_tpm_proxy_, SendU2fAttest(_, _)).WillOnce(Return(0));
  EXPECT_CALL(*mock_allowlisting_util_, AppendDataToCert(_))
      .WillOnce(Return(true));

  auto mock_method_response =
      std::make_unique<MockDBusMethodResponse<MakeCredentialResponse>>();
  bool called = false;
  mock_method_response->set_return_callback(base::Bind(
      [](bool* called_ptr, const MakeCredentialResponse& resp) {
        EXPECT_EQ(resp.status(), MakeCredentialResponse::SUCCESS);
        EXPECT_EQ(resp.attestation_format(), "fido-u2f");
        const std::string expected_attestation_statement =
            "A2"      // Start a CBOR map of 2 elements
            "63"      // Start CBOR text of 3 chars
            "736967"  // "sig"
            ".+"      // Random signature
            "63"      // Start CBOR text of 3 chars
            "783563"  // "x5c"
            "81"      // Start CBOR array of 1 element
            ".+";     // Random x509
        EXPECT_THAT(base::HexEncode(resp.attestation_statement().data(),
                                    resp.attestation_statement().size()),
                    MatchesRegex(expected_attestation_statement));
        *called_ptr = true;
      },
      &called));

  handler_->MakeCredential(std::move(mock_method_response), request);
  presence_requested_expected_ = 1;
  ASSERT_TRUE(called);
}

}  // namespace
}  // namespace u2f

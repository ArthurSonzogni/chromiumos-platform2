// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/conversion.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace arc::keymint {

namespace {

constexpr std::array<uint8_t, 3> kBlob1{{3, 23, 59}};
constexpr std::array<uint8_t, 4> kBlob2{{23, 46, 69, 92}};
constexpr std::array<uint8_t, 5> kBlob3{{1, 2, 3, 4, 5}};
constexpr int32_t kKeyMintMessageVersion = 4;
constexpr uint64_t fakeChallenge = 224498432732987237;
constexpr uint64_t fakeAuthenticatorId = 790712303;
constexpr uint64_t fakeUserId = 321;
constexpr uint64_t fakeTimeStamp = 4349843232312345627;

::testing::AssertionResult VerifyVectorUint8(const uint8_t* a,
                                             const size_t a_size,
                                             const std::vector<uint8_t>& b) {
  if (a_size != b.size()) {
    return ::testing::AssertionFailure()
           << "Sizes differ: a=" << a_size << " b=" << b.size();
  }
  for (size_t i = 0; i < a_size; ++i) {
    if (a[i] != b[i]) {
      return ::testing::AssertionFailure()
             << "Elements differ: a=" << static_cast<int>(a[i])
             << " b=" << static_cast<int>(b[i]);
    }
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifyKeyParametersWithStrictInputs(
    const ::keymaster::AuthorizationSet& a,
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& b) {
  if (a.size() != b.size()) {
    return ::testing::AssertionFailure()
           << "Sizes differ: a=" << a.size() << " b=" << b.size();
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tag != static_cast<uint32_t>(b[i]->tag)) {
      return ::testing::AssertionFailure()
             << "Tags differ: i=" << i
             << " a=" << static_cast<uint32_t>(a[i].tag) << " b=" << b[i]->tag;
    }
  }
  if (!(b[0]->value->is_bool_value() && b[1]->value->is_integer() &&
        b[2]->value->is_long_integer() && b[3]->value->is_date_time() &&
        b[4]->value->is_blob() && b[5]->value->is_algorithm())) {
    return ::testing::AssertionFailure() << "Incorrect union value type";
  }
  if (!(a[0].boolean == b[0]->value->get_bool_value() &&
        a[1].integer == b[1]->value->get_integer() &&
        a[2].long_integer == b[2]->value->get_long_integer() &&
        a[3].date_time == b[3]->value->get_date_time()) &&
      a[5].enumerated == static_cast<uint32_t>(b[5]->value->get_algorithm())) {
    return ::testing::AssertionFailure() << "Values differ";
  }
  return VerifyVectorUint8(a[4].blob.data, a[4].blob.data_length,
                           b[4]->value->get_blob());
}

std::vector<arc::mojom::keymint::KeyParameterPtr> KeyParameterVector() {
  std::vector<arc::mojom::keymint::KeyParameterPtr> parameters(6);
  // bool
  auto paramBool = arc::mojom::keymint::KeyParameterValue::NewBoolValue(true);
  parameters[0] = arc::mojom::keymint::KeyParameter::New(
      static_cast<arc::mojom::keymint::Tag>(KM_TAG_CALLER_NONCE),
      std::move(paramBool));
  // int, int_rep
  auto paramInt = arc::mojom::keymint::KeyParameterValue::NewInteger(128);
  parameters[1] = arc::mojom::keymint::KeyParameter::New(
      arc::mojom::keymint::Tag::KEY_SIZE, std::move(paramInt));
  // long
  auto paramLong =
      arc::mojom::keymint::KeyParameterValue::NewLongInteger(65537);
  parameters[2] = arc::mojom::keymint::KeyParameter::New(
      static_cast<arc::mojom::keymint::Tag>(KM_TAG_RSA_PUBLIC_EXPONENT),
      std::move(paramLong));
  // date
  auto paramDate = arc::mojom::keymint::KeyParameterValue::NewDateTime(1337);
  parameters[3] = arc::mojom::keymint::KeyParameter::New(
      static_cast<arc::mojom::keymint::Tag>(KM_TAG_ACTIVE_DATETIME),
      std::move(paramDate));
  // bignum, bytes
  auto paramBlob = arc::mojom::keymint::KeyParameterValue::NewBlob(
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()));
  parameters[4] = arc::mojom::keymint::KeyParameter::New(
      static_cast<arc::mojom::keymint::Tag>(KM_TAG_APPLICATION_DATA),
      std::move(paramBlob));
  // enum, enum_rep
  auto paramAlgo = arc::mojom::keymint::KeyParameterValue::NewAlgorithm(
      arc::mojom::keymint::Algorithm::TRIPLE_DES);
  parameters[5] = arc::mojom::keymint::KeyParameter::New(
      arc::mojom::keymint::Tag::ALGORITHM, std::move(paramAlgo));
  return parameters;
}

::arc::mojom::keymint::HardwareAuthTokenPtr CreateHardwareAuthToken() {
  return ::arc::mojom::keymint::HardwareAuthToken::New(
      fakeChallenge, fakeUserId, fakeAuthenticatorId,
      arc::mojom::keymint::HardwareAuthenticatorType(HW_AUTH_ANY),
      ::arc::mojom::keymint::Timestamp::New(fakeTimeStamp),
      std::vector<uint8_t>{2, 3});
}

::arc::mojom::keymint::TimeStampTokenPtr CreateTimeStampToken() {
  return ::arc::mojom::keymint::TimeStampToken::New(
      fakeChallenge, ::arc::mojom::keymint::Timestamp::New(fakeTimeStamp),
      std::vector<uint8_t>{2, 3});
}

::testing::AssertionResult VerifyHardwareAuthToken(
    const ::keymaster::AuthorizationSet& a,
    const arc::mojom::keymint::HardwareAuthToken& b) {
  auto result = ::testing::AssertionResult(false);
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tag == static_cast<uint32_t>(KM_TAG_AUTH_TOKEN)) {
      keymaster_blob_t blob = a[i].blob;
      auto tokenAsVec(authToken2AidlVec(b));
      result = VerifyVectorUint8(blob.data, blob.data_length, tokenAsVec);
    }
  }
  return result;
}

::testing::AssertionResult VerifyVerificationToken(
    ::keymaster::VerificationToken& km,
    const arc::mojom::keymint::TimeStampTokenPtr& mojo) {
  if (mojo.is_null()) {
    return ::testing::AssertionFailure()
           << "Verification Token failed as mojo is null";
  }
  if (mojo->timestamp.is_null()) {
    return ::testing::AssertionFailure()
           << "Verification Token failed as mojo timestamp is null";
  }
  if (km.challenge != mojo->challenge) {
    return ::testing::AssertionFailure()
           << "Verification Token challenge mismatch: km=" << km.challenge
           << " mojo=" << mojo->challenge;
  }
  if (km.timestamp != mojo->timestamp->milli_seconds) {
    return ::testing::AssertionFailure()
           << "Verification Token timestamp mismatch: km=" << km.timestamp
           << " mojo=" << mojo->timestamp->milli_seconds;
  }
  if (auto vectorMatch =
          VerifyVectorUint8(km.mac.data, km.mac.size(), mojo->mac);
      !vectorMatch) {
    return vectorMatch;
  }
  return ::testing::AssertionSuccess();
}

}  // namespace

TEST(ConvertFromKeymasterMessage, Uint8Vector) {
  // Convert.
  std::vector<uint8_t> output =
      ConvertFromKeymasterMessage(kBlob1.data(), kBlob1.size());

  // Verify.
  EXPECT_TRUE(VerifyVectorUint8(kBlob1.data(), kBlob1.size(), output));
}

TEST(ConvertFromKeymasterMessage, EnumConversionKeyFormat) {
  // Verify.
  EXPECT_EQ(KM_KEY_FORMAT_PKCS8,
            ConvertEnum(arc::mojom::keymint::KeyFormat::PKCS8));
  EXPECT_EQ(KM_KEY_FORMAT_X509,
            ConvertEnum(arc::mojom::keymint::KeyFormat::X509));
  EXPECT_EQ(KM_KEY_FORMAT_RAW,
            ConvertEnum(arc::mojom::keymint::KeyFormat::RAW));
}

TEST(ConvertFromKeymasterMessage, EnumConversionTag) {
  // Verify.
  // TODO(b/274723521): Add KM_TAG_HARDWARE_TYPE
  EXPECT_EQ(KM_TAG_PURPOSE, ConvertEnum(arc::mojom::keymint::Tag::PURPOSE));
  EXPECT_EQ(KM_TAG_ALGORITHM, ConvertEnum(arc::mojom::keymint::Tag::ALGORITHM));
  EXPECT_EQ(KM_TAG_KEY_SIZE, ConvertEnum(arc::mojom::keymint::Tag::KEY_SIZE));
  EXPECT_EQ(KM_TAG_BLOCK_MODE,
            ConvertEnum(arc::mojom::keymint::Tag::BLOCK_MODE));
  EXPECT_EQ(KM_TAG_DIGEST, ConvertEnum(arc::mojom::keymint::Tag::DIGEST));
  EXPECT_EQ(KM_TAG_PADDING, ConvertEnum(arc::mojom::keymint::Tag::PADDING));
  EXPECT_EQ(KM_TAG_CALLER_NONCE,
            ConvertEnum(arc::mojom::keymint::Tag::CALLER_NONCE));
  EXPECT_EQ(KM_TAG_MIN_MAC_LENGTH,
            ConvertEnum(arc::mojom::keymint::Tag::MIN_MAC_LENGTH));
  EXPECT_EQ(KM_TAG_EC_CURVE, ConvertEnum(arc::mojom::keymint::Tag::EC_CURVE));
  EXPECT_EQ(KM_TAG_RSA_PUBLIC_EXPONENT,
            ConvertEnum(arc::mojom::keymint::Tag::RSA_PUBLIC_EXPONENT));
  EXPECT_EQ(KM_TAG_INCLUDE_UNIQUE_ID,
            ConvertEnum(arc::mojom::keymint::Tag::INCLUDE_UNIQUE_ID));
  EXPECT_EQ(KM_TAG_RSA_OAEP_MGF_DIGEST,
            ConvertEnum(arc::mojom::keymint::Tag::RSA_OAEP_MGF_DIGEST));
  EXPECT_EQ(KM_TAG_BOOTLOADER_ONLY,
            ConvertEnum(arc::mojom::keymint::Tag::BOOTLOADER_ONLY));
  EXPECT_EQ(KM_TAG_ROLLBACK_RESISTANCE,
            ConvertEnum(arc::mojom::keymint::Tag::ROLLBACK_RESISTANCE));
  EXPECT_EQ(KM_TAG_EARLY_BOOT_ONLY,
            ConvertEnum(arc::mojom::keymint::Tag::EARLY_BOOT_ONLY));
  EXPECT_EQ(KM_TAG_ACTIVE_DATETIME,
            ConvertEnum(arc::mojom::keymint::Tag::ACTIVE_DATETIME));
  EXPECT_EQ(KM_TAG_ORIGINATION_EXPIRE_DATETIME,
            ConvertEnum(arc::mojom::keymint::Tag::ORIGINATION_EXPIRE_DATETIME));
  EXPECT_EQ(KM_TAG_USAGE_EXPIRE_DATETIME,
            ConvertEnum(arc::mojom::keymint::Tag::USAGE_EXPIRE_DATETIME));
  EXPECT_EQ(KM_TAG_MIN_SECONDS_BETWEEN_OPS,
            ConvertEnum(arc::mojom::keymint::Tag::MIN_SECONDS_BETWEEN_OPS));
  EXPECT_EQ(KM_TAG_MAX_USES_PER_BOOT,
            ConvertEnum(arc::mojom::keymint::Tag::MAX_USES_PER_BOOT));
  EXPECT_EQ(KM_TAG_USAGE_COUNT_LIMIT,
            ConvertEnum(arc::mojom::keymint::Tag::USAGE_COUNT_LIMIT));
  EXPECT_EQ(KM_TAG_USER_ID, ConvertEnum(arc::mojom::keymint::Tag::USER_ID));
  EXPECT_EQ(KM_TAG_USER_SECURE_ID,
            ConvertEnum(arc::mojom::keymint::Tag::USER_SECURE_ID));
  EXPECT_EQ(KM_TAG_NO_AUTH_REQUIRED,
            ConvertEnum(arc::mojom::keymint::Tag::NO_AUTH_REQUIRED));
  EXPECT_EQ(KM_TAG_USER_AUTH_TYPE,
            ConvertEnum(arc::mojom::keymint::Tag::USER_AUTH_TYPE));
  EXPECT_EQ(KM_TAG_AUTH_TIMEOUT,
            ConvertEnum(arc::mojom::keymint::Tag::AUTH_TIMEOUT));
  EXPECT_EQ(KM_TAG_ALLOW_WHILE_ON_BODY,
            ConvertEnum(arc::mojom::keymint::Tag::ALLOW_WHILE_ON_BODY));
  EXPECT_EQ(
      KM_TAG_TRUSTED_USER_PRESENCE_REQUIRED,
      ConvertEnum(arc::mojom::keymint::Tag::TRUSTED_USER_PRESENCE_REQUIRED));
  EXPECT_EQ(
      KM_TAG_TRUSTED_CONFIRMATION_REQUIRED,
      ConvertEnum(arc::mojom::keymint::Tag::TRUSTED_CONFIRMATION_REQUIRED));
  EXPECT_EQ(KM_TAG_UNLOCKED_DEVICE_REQUIRED,
            ConvertEnum(arc::mojom::keymint::Tag::UNLOCKED_DEVICE_REQUIRED));
  EXPECT_EQ(KM_TAG_APPLICATION_ID,
            ConvertEnum(arc::mojom::keymint::Tag::APPLICATION_ID));
  EXPECT_EQ(KM_TAG_APPLICATION_DATA,
            ConvertEnum(arc::mojom::keymint::Tag::APPLICATION_DATA));
  EXPECT_EQ(KM_TAG_CREATION_DATETIME,
            ConvertEnum(arc::mojom::keymint::Tag::CREATION_DATETIME));
  EXPECT_EQ(KM_TAG_ORIGIN, ConvertEnum(arc::mojom::keymint::Tag::ORIGIN));
  EXPECT_EQ(KM_TAG_ROOT_OF_TRUST,
            ConvertEnum(arc::mojom::keymint::Tag::ROOT_OF_TRUST));
  EXPECT_EQ(KM_TAG_OS_VERSION,
            ConvertEnum(arc::mojom::keymint::Tag::OS_VERSION));
  EXPECT_EQ(KM_TAG_OS_PATCHLEVEL,
            ConvertEnum(arc::mojom::keymint::Tag::OS_PATCHLEVEL));
  EXPECT_EQ(KM_TAG_UNIQUE_ID, ConvertEnum(arc::mojom::keymint::Tag::UNIQUE_ID));
  EXPECT_EQ(KM_TAG_ATTESTATION_CHALLENGE,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_CHALLENGE));
  EXPECT_EQ(KM_TAG_ATTESTATION_APPLICATION_ID,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_APPLICATION_ID));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_BRAND,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_BRAND));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_DEVICE,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_DEVICE));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_PRODUCT,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_PRODUCT));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_SERIAL,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_SERIAL));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_IMEI,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_IMEI));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_MEID,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_MEID));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_MANUFACTURER,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_MANUFACTURER));
  EXPECT_EQ(KM_TAG_ATTESTATION_ID_MODEL,
            ConvertEnum(arc::mojom::keymint::Tag::ATTESTATION_ID_MODEL));
  EXPECT_EQ(KM_TAG_VENDOR_PATCHLEVEL,
            ConvertEnum(arc::mojom::keymint::Tag::VENDOR_PATCHLEVEL));
  EXPECT_EQ(KM_TAG_BOOT_PATCHLEVEL,
            ConvertEnum(arc::mojom::keymint::Tag::BOOT_PATCHLEVEL));
  EXPECT_EQ(KM_TAG_DEVICE_UNIQUE_ATTESTATION,
            ConvertEnum(arc::mojom::keymint::Tag::DEVICE_UNIQUE_ATTESTATION));
  EXPECT_EQ(KM_TAG_IDENTITY_CREDENTIAL_KEY,
            ConvertEnum(arc::mojom::keymint::Tag::IDENTITY_CREDENTIAL_KEY));
  EXPECT_EQ(KM_TAG_STORAGE_KEY,
            ConvertEnum(arc::mojom::keymint::Tag::STORAGE_KEY));
  EXPECT_EQ(KM_TAG_ASSOCIATED_DATA,
            ConvertEnum(arc::mojom::keymint::Tag::ASSOCIATED_DATA));
  EXPECT_EQ(KM_TAG_NONCE, ConvertEnum(arc::mojom::keymint::Tag::NONCE));
  EXPECT_EQ(KM_TAG_MAC_LENGTH,
            ConvertEnum(arc::mojom::keymint::Tag::MAC_LENGTH));
  EXPECT_EQ(KM_TAG_RESET_SINCE_ID_ROTATION,
            ConvertEnum(arc::mojom::keymint::Tag::RESET_SINCE_ID_ROTATION));
  EXPECT_EQ(KM_TAG_CONFIRMATION_TOKEN,
            ConvertEnum(arc::mojom::keymint::Tag::CONFIRMATION_TOKEN));
  EXPECT_EQ(KM_TAG_CERTIFICATE_SERIAL,
            ConvertEnum(arc::mojom::keymint::Tag::CERTIFICATE_SERIAL));
  EXPECT_EQ(KM_TAG_CERTIFICATE_SUBJECT,
            ConvertEnum(arc::mojom::keymint::Tag::CERTIFICATE_SUBJECT));
  EXPECT_EQ(KM_TAG_CERTIFICATE_NOT_BEFORE,
            ConvertEnum(arc::mojom::keymint::Tag::CERTIFICATE_NOT_BEFORE));
  EXPECT_EQ(KM_TAG_CERTIFICATE_NOT_AFTER,
            ConvertEnum(arc::mojom::keymint::Tag::CERTIFICATE_NOT_AFTER));
  EXPECT_EQ(KM_TAG_MAX_BOOT_LEVEL,
            ConvertEnum(arc::mojom::keymint::Tag::MAX_BOOT_LEVEL));
}

TEST(ConvertFromKeymasterMessage, EnumConversionKeyPurpose) {
  // Verify.
  EXPECT_EQ(KM_PURPOSE_ENCRYPT,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::ENCRYPT));
  EXPECT_EQ(KM_PURPOSE_DECRYPT,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::DECRYPT));
  EXPECT_EQ(KM_PURPOSE_SIGN,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::SIGN));
  EXPECT_EQ(KM_PURPOSE_VERIFY,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::VERIFY));
  // TODO(b/274723521): Find why KM_PURPOSE_DERIVE_KEY doesn't exist in AIDL.
  EXPECT_EQ(KM_PURPOSE_WRAP,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::WRAP_KEY));
  EXPECT_EQ(KM_PURPOSE_AGREE_KEY,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::AGREE_KEY));
  EXPECT_EQ(KM_PURPOSE_ATTEST_KEY,
            ConvertEnum(arc::mojom::keymint::KeyPurpose::ATTEST_KEY));
}

TEST(ConvertFromKeymasterMessage, KeyParameterVector) {
  // Prepare.
  ::keymaster::AuthorizationSet input;
  input.push_back(keymaster_param_bool(KM_TAG_EARLY_BOOT_ONLY));  // bool
  input.push_back(keymaster_param_int(KM_TAG_KEY_SIZE,
                                      128));  // int, int_rep
  input.push_back(keymaster_param_long(KM_TAG_USER_SECURE_ID, 65537));  // long
  input.push_back(
      keymaster_param_date(KM_TAG_USAGE_EXPIRE_DATETIME, 1507));  // date
  input.push_back(keymaster_param_blob(KM_TAG_APPLICATION_DATA, kBlob1.data(),
                                       kBlob1.size()));  // bignum, bytes
  input.push_back(
      keymaster_param_enum(KM_TAG_ALGORITHM,
                           KM_ALGORITHM_TRIPLE_DES));  // enum, enum_rep

  // Convert.
  std::vector<arc::mojom::keymint::KeyParameterPtr> output =
      ConvertFromKeymasterMessage(input);

  // Verify.
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(input, output));
}

TEST(ConvertToKeymasterMessage, Buffer) {
  // Prepare.
  std::vector<uint8_t> input(kBlob1.begin(), kBlob1.end());

  // Convert.
  ::keymaster::Buffer buffer;
  ConvertToKeymasterMessage(input, &buffer);
  uint8_t output[kBlob1.size()];

  // Verify.
  EXPECT_TRUE(buffer.read(output, input.size()));
  EXPECT_TRUE(VerifyVectorUint8(output, input.size(), input));
}

TEST(ConvertToKeymasterMessage, ReusedBuffer) {
  // Prepare.
  std::vector<uint8_t> input1(kBlob1.begin(), kBlob1.end());
  std::vector<uint8_t> input2(kBlob2.begin(), kBlob2.end());

  // Convert.
  ::keymaster::Buffer buffer;
  ConvertToKeymasterMessage(input1, &buffer);
  ConvertToKeymasterMessage(input2, &buffer);
  uint8_t output[kBlob2.size()];

  // Verify.
  EXPECT_TRUE(buffer.read(output, kBlob2.size()));
  EXPECT_TRUE(VerifyVectorUint8(output, kBlob2.size(), input2));
}

TEST(ConvertToKeymasterMessage, ClientIdAndAppData) {
  // Prepare.
  std::vector<uint8_t> clientId(kBlob1.begin(), kBlob1.end());
  std::vector<uint8_t> appData(kBlob2.begin(), kBlob2.end());

  // Convert.
  ::keymaster::AuthorizationSet output;
  ConvertToKeymasterMessage(clientId, appData, &output);

  // Verify.
  ASSERT_EQ(2, output.size());
  EXPECT_EQ(KM_TAG_APPLICATION_ID, output[0].tag);
  EXPECT_EQ(KM_TAG_APPLICATION_DATA, output[1].tag);
  EXPECT_TRUE(VerifyVectorUint8(output[0].blob.data, output[0].blob.data_length,
                                clientId));
  EXPECT_TRUE(VerifyVectorUint8(output[1].blob.data, output[1].blob.data_length,
                                appData));
}

TEST(ConvertToKeymasterMessage, GetKeyCharacteristicsRequest) {
  // Prepare.
  auto input = ::arc::mojom::keymint::GetKeyCharacteristicsRequest::New(
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()),
      std::vector<uint8_t>(kBlob2.begin(), kBlob2.end()),
      std::vector<uint8_t>(kBlob3.begin(), kBlob3.end()));

  // Convert.
  auto output = MakeGetKeyCharacteristicsRequest(input, kKeyMintMessageVersion);

  // Verify.
  EXPECT_TRUE(VerifyVectorUint8(output->key_blob.key_material,
                                output->key_blob.key_material_size,
                                input->key_blob));
  ASSERT_EQ(output->additional_params.size(), 2);
  EXPECT_TRUE(VerifyVectorUint8(output->additional_params[0].blob.data,
                                output->additional_params[0].blob.data_length,
                                input->app_id));
  EXPECT_TRUE(VerifyVectorUint8(output->additional_params[1].blob.data,
                                output->additional_params[1].blob.data_length,
                                input->app_data));
}

TEST(ConvertToKeymasterMessage, GenerateKeyRequest) {
  // Prepare.
  std::vector<arc::mojom::keymint::KeyParameterPtr> input =
      KeyParameterVector();

  // Convert.
  auto output = MakeGenerateKeyRequest(input, kKeyMintMessageVersion);

  // Verify.
  EXPECT_TRUE(
      VerifyKeyParametersWithStrictInputs(output->key_description, input));
}

TEST(ConvertToMessage, ImportKeyRequest) {
  // Prepare.
  auto input = arc::mojom::keymint::ImportKeyRequest::New(
      KeyParameterVector(), arc::mojom::keymint::KeyFormat::PKCS8,
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()),
      arc::mojom::keymint::AttestationKeyPtr());

  // Convert.
  auto output = MakeImportKeyRequest(std::move(input), kKeyMintMessageVersion);

  // Verify.
  EXPECT_EQ(static_cast<keymaster_key_format_t>(input->key_format),
            output->key_format);
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(output->key_description,
                                                  input->key_params));
  // TODO(b/289173356): Verify Attest Key here as well.
  EXPECT_TRUE(VerifyVectorUint8(output->key_data.key_material,
                                output->key_data.key_material_size,
                                input->key_data));
}

TEST(ConvertToMessage, ImportWrappedKeyRequest) {
  // Prepare.
  uint64_t password_sid = 703710923123;
  uint64_t biometric_sid = 2702433194597;
  auto input = arc::mojom::keymint::ImportWrappedKeyRequest::New(
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()),
      std::vector<uint8_t>(kBlob2.begin(), kBlob2.end()),
      std::vector<uint8_t>(kBlob3.begin(), kBlob3.end()), KeyParameterVector(),
      password_sid, biometric_sid);

  // Convert.
  auto output = MakeImportWrappedKeyRequest(input, kKeyMintMessageVersion);

  // Verify.
  ASSERT_TRUE(output);
  EXPECT_TRUE(VerifyVectorUint8(output->wrapped_key.key_material,
                                output->wrapped_key.key_material_size,
                                input->wrapped_key_data));
  EXPECT_TRUE(VerifyVectorUint8(output->wrapping_key.key_material,
                                output->wrapping_key.key_material_size,
                                input->wrapping_key_blob));
  EXPECT_TRUE(VerifyVectorUint8(output->masking_key.key_material,
                                output->masking_key.key_material_size,
                                input->masking_key));
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(output->additional_params,
                                                  input->unwrapping_params));
  EXPECT_EQ(output->password_sid, input->password_sid);
  EXPECT_EQ(output->biometric_sid, input->biometric_sid);
}

TEST(ConvertToMessage, UpgradeKeyRequest) {
  // Prepare.
  auto input = arc::mojom::keymint::UpgradeKeyRequest::New(
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()), KeyParameterVector());

  // Convert.
  auto output = MakeUpgradeKeyRequest(std::move(input), kKeyMintMessageVersion);

  // Verify.
  EXPECT_TRUE(VerifyVectorUint8(output->key_blob.key_material,
                                output->key_blob.key_material_size,
                                input->key_blob_to_upgrade));
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(output->upgrade_params,
                                                  input->upgrade_params));
}

TEST(ConvertToKeymasterMessage, BeginOperationRequest) {
  // Prepare.
  ::arc::mojom::keymint::HardwareAuthTokenPtr auth_token_ptr =
      CreateHardwareAuthToken();
  auto input = arc::mojom::keymint::BeginRequest::New(
      arc::mojom::keymint::KeyPurpose::AGREE_KEY,
      std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()), KeyParameterVector(),
      std::move(auth_token_ptr));

  // Convert.
  auto output =
      MakeBeginOperationRequest(std::move(input), kKeyMintMessageVersion);

  // Verify.
  EXPECT_EQ(output->purpose,
            static_cast<keymaster_purpose_t>(input->key_purpose));
  EXPECT_TRUE(VerifyVectorUint8(output->key_blob.key_material,
                                output->key_blob.key_material_size,
                                input->key_blob));
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(output->additional_params,
                                                  input->params));
  EXPECT_TRUE(
      VerifyHardwareAuthToken(output->additional_params, *input->auth_token));
}

TEST(ConvertToMessage, UpdateOperationRequest) {
  // Prepare.
  ::arc::mojom::keymint::HardwareAuthTokenPtr auth_token_ptr =
      CreateHardwareAuthToken();

  ::arc::mojom::keymint::TimeStampTokenPtr time_token_ptr =
      CreateTimeStampToken();

  auto input = arc::mojom::keymint::UpdateRequest::New(
      65537, std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()),
      std::move(auth_token_ptr), std::move(time_token_ptr));

  // Convert.
  auto output = MakeUpdateOperationRequest(input, kKeyMintMessageVersion);

  // Verify.
  // We are not verifying the TimeStampTokenPtr now as it is not
  // used in the KeyMint Reference implementation.
  EXPECT_EQ(output->op_handle, input->op_handle);
  EXPECT_TRUE(VerifyVectorUint8(output->input.begin(),
                                output->input.available_read(), input->input));
  EXPECT_TRUE(
      VerifyHardwareAuthToken(output->additional_params, *input->auth_token));
}

TEST(ConvertToMessage, DeviceLockedRequest) {
  // Prepare.
  bool password_only = true;
  ::arc::mojom::keymint::TimeStampTokenPtr time_token_ptr =
      CreateTimeStampToken();

  // Convert.
  auto output = MakeDeviceLockedRequest(password_only, time_token_ptr,
                                        kKeyMintMessageVersion);

  // Verify.
  ASSERT_TRUE(output);
  EXPECT_EQ(output->passwordOnly, password_only);
  EXPECT_TRUE(VerifyVerificationToken(output->token, time_token_ptr));
}

TEST(ConvertToMessage, FinishOperationRequest) {
  // Prepare.
  ::arc::mojom::keymint::HardwareAuthTokenPtr auth_token_ptr =
      CreateHardwareAuthToken();

  ::arc::mojom::keymint::TimeStampTokenPtr time_token_ptr =
      CreateTimeStampToken();

  auto input = arc::mojom::keymint::FinishRequest::New(
      65537, std::vector<uint8_t>(kBlob1.begin(), kBlob1.end()),
      std::vector<uint8_t>(kBlob2.begin(), kBlob2.end()),
      std::move(auth_token_ptr), std::move(time_token_ptr),
      std::vector<uint8_t>(kBlob3.begin(), kBlob3.end()));

  // Convert.
  auto output = MakeFinishOperationRequest(input, kKeyMintMessageVersion);

  // Verify.
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  EXPECT_EQ(output->op_handle, input->op_handle);
  EXPECT_TRUE(VerifyVectorUint8(output->input.begin(),
                                output->input.available_read(),
                                input->input.value()));
  EXPECT_TRUE(VerifyVectorUint8(output->signature.begin(),
                                output->signature.available_read(),
                                input->signature.value()));
  EXPECT_TRUE(
      VerifyHardwareAuthToken(output->additional_params, *input->auth_token));
}

}  // namespace arc::keymint

// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_for_test.h>

#include "sealed_storage/sealed_storage.h"

using testing::_;
using testing::AtLeast;
using testing::Expectation;
using testing::ExpectationSet;
using testing::InSequence;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

constexpr uint8_t kRandomByte = 0x12;
constexpr uint16_t kExpectedIVSize = 16; /* AES IV size */
constexpr uint8_t kDftPolicyFill = 0x23;
constexpr uint8_t kWrongPolicyFill = 0x45;
constexpr size_t kPolicyDigestSize = 32; /* SHA-256 digest */
constexpr size_t kPcrValueSize = 32;     /* SHA-256 digest */
constexpr uint32_t kNvramCounterSize = 8;
constexpr uint32_t kNvramCounterOffset = 0;
constexpr uint32_t kNvramIndex = 0x100e;

bool operator==(const trunks::TPM2B_ECC_PARAMETER& rh,
                const trunks::TPM2B_ECC_PARAMETER& lh) {
  return rh.size == lh.size && memcmp(rh.buffer, lh.buffer, rh.size) == 0;
}
bool operator==(const trunks::TPMS_ECC_POINT& rh,
                const trunks::TPMS_ECC_POINT& lh) {
  return rh.x == lh.x && rh.y == lh.y;
}
bool operator==(const trunks::TPM2B_ECC_POINT& rh,
                const trunks::TPM2B_ECC_POINT& lh) {
  return rh.size == lh.size && rh.point == lh.point;
}
bool operator==(const trunks::TPM2B_DIGEST& rh,
                const trunks::TPM2B_DIGEST& lh) {
  return rh.size == lh.size && memcmp(rh.buffer, lh.buffer, rh.size) == 0;
}

MATCHER_P(Equals, value, "") {
  return arg == value;
}

namespace sealed_storage {

class SealedStorageTest : public ::testing::Test {
 public:
  SealedStorageTest() = default;
  SealedStorageTest(const SealedStorageTest&) = delete;
  SealedStorageTest& operator=(const SealedStorageTest&) = delete;

  virtual ~SealedStorageTest() = default;

  static SecretData DftDataToSeal() { return SecretData("testdata"); }

  static std::string DftPolicyDigest() {
    return std::string(kPolicyDigestSize, kDftPolicyFill);
  }

  static std::string WrongPolicyDigest() {
    return std::string(kPolicyDigestSize, kWrongPolicyFill);
  }

  static trunks::TPM2B_ECC_POINT GetECPointWithFilledXY(uint8_t x_fill,
                                                        uint8_t y_fill) {
    const trunks::TPMS_ECC_POINT point = {
        trunks::Make_TPM2B_ECC_PARAMETER(
            std::string(MAX_ECC_KEY_BYTES, x_fill)),
        trunks::Make_TPM2B_ECC_PARAMETER(
            std::string(MAX_ECC_KEY_BYTES, y_fill))};
    return trunks::Make_TPM2B_ECC_POINT(point);
  }

  static trunks::TPM2B_ECC_POINT DftPubPoint() {
    return GetECPointWithFilledXY(0x11, 0x22);
  }

  static trunks::TPM2B_ECC_POINT DftZPoint() {
    return GetECPointWithFilledXY(0x33, 0x44);
  }

  static trunks::TPM2B_ECC_POINT WrongZPoint() {
    return GetECPointWithFilledXY(0x55, 0x66);
  }

  static Policy ConstructEmptyPolicy() { return {}; }

  static Policy ConstructPcrBoundPolicy() {
    Policy::PcrMap pcr_map;

    for (uint8_t pcr = 0; pcr < 10; pcr++) {
      pcr_map.emplace(pcr, ConstructPcrValue(pcr));
    }

    return {.pcr_map = pcr_map};
  }

  static std::string ConstructPcrValue(uint8_t pcr) {
    return std::string(kPcrValueSize, pcr ^ 0xFF);
  }

  static Policy ConstructSecretBoundPolicy(std::string secret) {
    return {.secret = SecretData(secret)};
  }

  static Policy ConstructCounterBoundPolicy(uint32_t index) {
    return {.nvram_counter_index = index};
  }

  static Policy ConstructSecretAndPcrBoundPolicy(std::string secret) {
    Policy pcr_bound_policy = ConstructPcrBoundPolicy();
    Policy secret_bound_policy = ConstructSecretBoundPolicy(secret);
    return {.pcr_map = pcr_bound_policy.pcr_map,
            .secret = secret_bound_policy.secret};
  }

  static Policy ConstructCounterAndPcrBoundPolicy(uint32_t index) {
    Policy pcr_bound_policy = ConstructPcrBoundPolicy();
    Policy counter_bound_policy = ConstructCounterBoundPolicy(index);
    return {.pcr_map = pcr_bound_policy.pcr_map,
            .nvram_counter_index = counter_bound_policy.nvram_counter_index};
  }

  // Convert the sealed data blob of the default version produced by Seal()
  // into V1 blob.
  static void ConvertToV1(Data* sealed_data) {
    constexpr size_t kAdditionalV2DataSize =
        /* plain size */ sizeof(uint16_t) +
        /* policy digest */ sizeof(uint16_t) + kPolicyDigestSize;
    (*sealed_data)[0] = 0x01;
    sealed_data->erase(sealed_data->cbegin() + 1,
                       sealed_data->cbegin() + 1 + kAdditionalV2DataSize);
  }

  // Sets up a pair of ZPoints returned from KeyGen and ZGen with the following
  // properties: if you encrypt a particular data_to_seal with the first ZPoint
  // (returned from KeyGen) and then decrypt it with the second ZPoint (returned
  // from ZGen), decryption returns success as it produces valid padding (but
  // not the same data, of course).
  // Returns data_to_sign to be used with the setup ZPoints.
  const SecretData SetupWrongZPointWithGarbageData() {
    z_point_ = GetECPointWithFilledXY(0x11, 0x11);          // KeyGen
    z_gen_out_point_ = GetECPointWithFilledXY(0x0F, 0x00);  // ZGen
    return SecretData("testdata");
  }

  void SetUp() override {
    trunks_factory_.set_tpm(&tpm_);
    trunks_factory_.set_tpm_utility(&tpm_utility_);
    trunks_factory_.set_policy_session(&policy_session_);
    trunks_factory_.set_trial_session(&trial_policy_session_);

    SetUpMockCalls();
  }

  void SetUpMockCalls() {
    ON_CALL(tpm_ownership_, GetTpmStatus)
        .WillByDefault(
            Invoke([this](const tpm_manager::GetTpmStatusRequest& request,
                          tpm_manager::GetTpmStatusReply* reply,
                          brillo::ErrorPtr*, int) {
              reply->set_status(tpm_manager_result_);
              reply->mutable_local_data()->set_endorsement_password(
                  endorsement_password_);
              return true;
            }));

    ON_CALL(tpm_, CreatePrimarySyncShort)
        .WillByDefault(
            Invoke(this, &SealedStorageTest::CreatePrimarySyncShort));
    ON_CALL(tpm_, ECDH_KeyGenSync)
        .WillByDefault(Invoke(this, &SealedStorageTest::ECDH_KeyGenSync));
    ON_CALL(tpm_, ECDH_ZGenSync)
        .WillByDefault(Invoke(this, &SealedStorageTest::ECDH_ZGenSync));
    ON_CALL(tpm_, GetRandomSync)
        .WillByDefault(Invoke(this, &SealedStorageTest::GetRandomSync));
    ON_CALL(tpm_utility_, ReadNVSpace)
        .WillByDefault(Invoke(this, &SealedStorageTest::ReadNVSpace));
    ON_CALL(tpm_utility_, AddPcrValuesToPolicySession)
        .WillByDefault(
            Invoke(this, &SealedStorageTest::AddPcrValuesToPolicySession));
    ON_CALL(policy_session_, PolicyPCR)
        .WillByDefault(Return(policy_pcr_result_));
    ON_CALL(policy_session_, GetDelegate)
        .WillByDefault(Return(&auth_delegate_));
    ON_CALL(policy_session_, GetDigest)
        .WillByDefault(Invoke(this, &SealedStorageTest::GetDigest));
    ON_CALL(policy_session_, PolicyOR).WillByDefault(Return(policy_or_result_));
    ON_CALL(policy_session_, PolicyNV).WillByDefault(Return(policy_nv_result_));
    ON_CALL(policy_session_, StartUnboundSession)
        .WillByDefault(Return(start_session_result_));
    ON_CALL(trial_policy_session_, PolicyPCR)
        .WillByDefault(Return(trial_policy_pcr_result_));
    ON_CALL(trial_policy_session_, GetDelegate)
        .WillByDefault(Return(&auth_delegate_));
    ON_CALL(trial_policy_session_, GetDigest)
        .WillByDefault(Invoke(this, &SealedStorageTest::GetDigest));
    ON_CALL(trial_policy_session_, PolicyOR)
        .WillByDefault(Return(trial_policy_or_result_));
    ON_CALL(trial_policy_session_, PolicyNV)
        .WillByDefault(Return(trial_policy_nv_result_));
    ON_CALL(trial_policy_session_, StartUnboundSession)
        .WillByDefault(Return(start_session_result_));
  }

  trunks::TPM_RC CreatePrimarySyncShort(
      const trunks::TPMI_RH_HIERARCHY& primary_handle,
      const trunks::TPM2B_PUBLIC& in_public,
      const trunks::TPML_PCR_SELECTION& creation_pcr,
      trunks::TPM_HANDLE* object_handle,
      trunks::TPM2B_PUBLIC* out_public,
      trunks::TPM2B_CREATION_DATA* creation_data,
      trunks::TPM2B_DIGEST* creation_hash,
      trunks::TPMT_TK_CREATION* creation_ticket,
      trunks::TPM2B_NAME* name,
      trunks::AuthorizationDelegate* authorization_delegate) {
    create_primary_public_area_ = in_public;
    *object_handle = sealing_key_handle_;
    return create_primary_result_;
  }

  trunks::TPM_RC ECDH_KeyGenSync(
      const trunks::TPMI_DH_OBJECT& key_handle,
      const std::string& key_handle_name,
      trunks::TPM2B_ECC_POINT* z_point,
      trunks::TPM2B_ECC_POINT* pub_point,
      trunks::AuthorizationDelegate* authorization_delegate) {
    *z_point = z_point_;
    *pub_point = pub_point_;
    return key_gen_result_;
  }

  trunks::TPM_RC ECDH_ZGenSync(
      const trunks::TPMI_DH_OBJECT& key_handle,
      const std::string& key_handle_name,
      const trunks::TPM2B_ECC_POINT& in_point,
      trunks::TPM2B_ECC_POINT* out_point,
      trunks::AuthorizationDelegate* authorization_delegate) {
    *out_point = z_gen_out_point_;
    z_gen_in_point_ = in_point;
    return z_gen_result_;
  }

  trunks::TPM_RC ReadNVSpace(uint32_t index,
                             uint32_t offset,
                             size_t num_bytes,
                             bool using_owner_authorization,
                             std::string* nvram_data,
                             trunks::AuthorizationDelegate* delegate) {
    *nvram_data = counter_space_value_;
    return read_nv_result_;
  }

  trunks::TPM_RC AddPcrValuesToPolicySession(
      const std::map<uint32_t, std::string>& pcr_map,
      bool use_auth_value,
      trunks::PolicySession* policy_session) {
    return policy_session->PolicyPCR(pcr_map);
  }

  trunks::TPM_RC GetRandomSync(
      const trunks::UINT16& bytes_requested,
      trunks::TPM2B_DIGEST* random_bytes,
      trunks::AuthorizationDelegate* authorization_delegate) {
    if (random_.has_value()) {
      *random_bytes = trunks::Make_TPM2B_DIGEST(random_.value());
    } else {
      random_bytes->size = bytes_requested;
      memset(random_bytes->buffer, kRandomByte, bytes_requested);
    }
    return get_random_result_;
  }

  trunks::TPM_RC GetDigest(std::string* policy_digest) {
    *policy_digest = policy_digest_;
    return get_policy_digest_result_;
  }

  void ExpectCommandSequence(bool do_seal = true, bool do_unseal = true) {
    testing::InSequence s;

    if (do_seal) {
      /* Seal: Create sealing key */
      EXPECT_CALL(tpm_ownership_, GetTpmStatus);

      bool failure = false;
      ExpectTrialPolicySequence(&failure);
      if (failure) {
        return;
      }

      if (tpm_manager_result_ != tpm_manager::STATUS_SUCCESS) {
        /* In case of GetTpmStatus error... */
        EXPECT_CALL(tpm_, CreatePrimarySyncShort).Times(0);
        return;
      }

      EXPECT_CALL(tpm_, CreatePrimarySyncShort(trunks::TPM_RH_ENDORSEMENT, _, _,
                                               _, _, _, _, _, _, _));
      if (create_primary_result_ != trunks::TPM_RC_SUCCESS) {
        /* In case of CreatePrimary error... */
        EXPECT_CALL(tpm_, ECDH_KeyGenSync).Times(0);
        return;
      }

      /* Seal: Generate seeds */
      EXPECT_CALL(tpm_, ECDH_KeyGenSync(sealing_key_handle_, _, _, _, _));
      EXPECT_CALL(tpm_, GetRandomSync(kExpectedIVSize, _, _));
    }

    if (do_unseal) {
      /* Unseal: Create sealing key */
      EXPECT_CALL(tpm_ownership_, GetTpmStatus);
      if (tpm_manager_result_ != tpm_manager::STATUS_SUCCESS) {
        /* In case of GetTpmStatus error... */
        EXPECT_CALL(tpm_, CreatePrimarySyncShort).Times(0);
        return;
      }

      bool failure = false;
      ExpectTrialPolicySequence(&failure);
      if (failure) {
        return;
      }

      EXPECT_CALL(tpm_, CreatePrimarySyncShort(trunks::TPM_RH_ENDORSEMENT, _, _,
                                               _, _, _, _, _, _, _));
      if (create_primary_result_ != trunks::TPM_RC_SUCCESS) {
        /* In case of CreatePrimary error... */
        EXPECT_CALL(tpm_, ECDH_ZGenSync).Times(0);
        return;
      }

      /* Unseal: Restore seeds */
      ExpectPolicySequence(&failure);
      if (failure) {
        EXPECT_CALL(tpm_, ECDH_ZGenSync).Times(0);
        return;
      }

      EXPECT_CALL(tpm_, ECDH_ZGenSync(sealing_key_handle_, _,
                                      Equals(pub_point_), _, &auth_delegate_));
    }
  }

  void ExpectTrialPolicySequence(bool* failure) {
    EXPECT_CALL(trial_policy_session_, StartUnboundSession);
    if (!policy_.pcr_map.empty()) {
      EXPECT_CALL(tpm_utility_, AddPcrValuesToPolicySession);
      EXPECT_CALL(trial_policy_session_, PolicyPCR(policy_.pcr_map));
      if (trial_policy_pcr_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
    }
    if (!policy_.secret.empty()) {
      EXPECT_CALL(trial_policy_session_, PolicyOR);
      if (trial_policy_or_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
    }
    if (policy_.nvram_counter_index.has_value()) {
      EXPECT_CALL(tpm_utility_,
                  ReadNVSpace(*policy_.nvram_counter_index, kNvramCounterOffset,
                              kNvramCounterSize, false, _, _));
      if (read_nv_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
      EXPECT_CALL(
          trial_policy_session_,
          PolicyNV(*policy_.nvram_counter_index, kNvramCounterOffset, false,
                   Equals(trunks::Make_TPM2B_DIGEST(counter_space_value_)),
                   counter_operation_, _));
      if (trial_policy_nv_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
    }
    return;
  }

  void ExpectPolicySequence(bool* failure) {
    EXPECT_CALL(policy_session_, StartUnboundSession);
    if (!policy_.pcr_map.empty()) {
      EXPECT_CALL(policy_session_, PolicyPCR(policy_.pcr_map));
      if (policy_pcr_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
    }
    if (!policy_.secret.empty()) {
      EXPECT_CALL(policy_session_, PolicyOR);
    }
    if (policy_.nvram_counter_index.has_value()) {
      EXPECT_CALL(tpm_utility_,
                  ReadNVSpace(*policy_.nvram_counter_index, kNvramCounterOffset,
                              kNvramCounterSize, false, _, _));
      if (read_nv_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
      EXPECT_CALL(
          policy_session_,
          PolicyNV(*policy_.nvram_counter_index, kNvramCounterOffset, false,
                   Equals(trunks::Make_TPM2B_DIGEST(counter_space_value_)),
                   counter_operation_, _));
      if (policy_nv_result_ != trunks::TPM_RC_SUCCESS) {
        *failure = true;
        return;
      }
    }

    EXPECT_CALL(policy_session_, GetDelegate());
  }

  void ResetMocks() {
    Mock::VerifyAndClearExpectations(&tpm_ownership_);
    Mock::VerifyAndClearExpectations(&tpm_utility_);
    Mock::VerifyAndClearExpectations(&tpm_);
    Mock::VerifyAndClearExpectations(&policy_session_);
    Mock::VerifyAndClearExpectations(&trial_policy_session_);
  }

  void SealUnseal(bool expect_seal_success,
                  bool expect_unseal_success,
                  const SecretData& data_to_seal) {
    sealed_storage_.reset_policy(policy_);
    auto sealed_data = sealed_storage_.Seal(data_to_seal);
    EXPECT_EQ(sealed_data.has_value(), expect_seal_success);
    if (!sealed_data.has_value()) {
      return;
    }

    auto result = sealed_storage_.Unseal(sealed_data.value());
    EXPECT_EQ(result.has_value(), expect_unseal_success);
    if (expect_unseal_success && result.has_value()) {
      EXPECT_EQ(result.value(), data_to_seal);
    }
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};
  Policy policy_;
  trunks::MockTpm tpm_;
  trunks::MockTpmUtility tpm_utility_;
  trunks::MockAuthorizationDelegate auth_delegate_;
  NiceMock<trunks::MockPolicySession> policy_session_;
  NiceMock<trunks::MockPolicySession> trial_policy_session_;
  trunks::TrunksFactoryForTest trunks_factory_;
  testing::StrictMock<org::chromium::TpmManagerProxyMock> tpm_ownership_;
  SealedStorage sealed_storage_{policy_, &trunks_factory_, &tpm_ownership_};

  tpm_manager::TpmManagerStatus tpm_manager_result_ =
      tpm_manager::STATUS_SUCCESS;
  std::string endorsement_password_ = "endorsement_password";

  trunks::TPM_RC create_primary_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_HANDLE sealing_key_handle_ = trunks::TRANSIENT_FIRST;
  trunks::TPM2B_PUBLIC create_primary_public_area_ = {};

  trunks::TPM_RC key_gen_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM2B_ECC_POINT z_point_ = DftZPoint();
  trunks::TPM2B_ECC_POINT pub_point_ = DftPubPoint();

  trunks::TPM_RC z_gen_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM2B_ECC_POINT z_gen_out_point_ = DftZPoint();
  trunks::TPM2B_ECC_POINT z_gen_in_point_ = {};

  trunks::TPM_RC get_random_result_ = trunks::TPM_RC_SUCCESS;
  base::Optional<std::string> random_ = {};

  trunks::TPM_RC get_policy_digest_result_ = trunks::TPM_RC_SUCCESS;
  std::string policy_digest_ = DftPolicyDigest();

  std::string counter_space_value_ = "23";
  trunks::TPM_EO counter_operation_ = trunks::TPM_EO_EQ;

  trunks::TPM_RC trial_policy_pcr_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC trial_policy_or_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC trial_policy_nv_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC policy_pcr_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC policy_or_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC policy_nv_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_RC read_nv_result_ = trunks::TPM_RC_SUCCESS;

  trunks::TPM_RC start_session_result_ = trunks::TPM_RC_SUCCESS;
};

TEST_F(SealedStorageTest, TrivialPolicySuccess) {
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, VariousPlaintextSizesSuccess) {
  for (size_t data_size = 0; data_size <= 65; data_size++) {
    std::string data(data_size, 'x');
    ExpectCommandSequence();
    SealUnseal(true, true, SecretData(data));
    ResetMocks();
  }
}

TEST_F(SealedStorageTest, PcrBoundPolicySuccess) {
  policy_ = ConstructPcrBoundPolicy();
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, SecretBoundPolicySuccess) {
  policy_ = ConstructSecretBoundPolicy("secret");
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, SecretAndPcrBoundPolicySuccess) {
  policy_ = ConstructSecretAndPcrBoundPolicy("secret");
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, CounterBoundPolicySuccess) {
  policy_ = ConstructCounterBoundPolicy(kNvramIndex);
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, CounterAndPcrBoundPolicySuccess) {
  policy_ = ConstructCounterAndPcrBoundPolicy(kNvramIndex);
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, CounterBoundPolicyTrialFailure) {
  policy_ = ConstructCounterBoundPolicy(kNvramIndex);
  trial_policy_nv_result_ = trunks::TPM_RC_VALUE;
  SetUpMockCalls();
  ExpectCommandSequence();
  SealUnseal(false, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, CounterBoundReadNVFailure) {
  policy_ = ConstructCounterBoundPolicy(kNvramIndex);
  read_nv_result_ = trunks::TPM_RC_VALUE;
  SetUpMockCalls();
  ExpectCommandSequence();
  SealUnseal(false, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, CounterBoundPolicyFailure) {
  policy_ = ConstructCounterBoundPolicy(kNvramIndex);
  policy_nv_result_ = trunks::TPM_RC_VALUE;
  SetUpMockCalls();
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongRestoredZPointError) {
  z_gen_out_point_ = WrongZPoint();
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongDeviceStateError) {
  policy_ = ConstructPcrBoundPolicy();
  policy_pcr_result_ = trunks::TPM_RC_VALUE;
  SetUpMockCalls();
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongRestoredZPointGarbage) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  ExpectCommandSequence();
  SealUnseal(true, false, data_to_seal);
}

TEST_F(SealedStorageTest, WrongPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with some initial policy digest.
  policy_digest_ = DftPolicyDigest();
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with a different policy, resulting in a different digest.
  policy_digest_ = WrongPolicyDigest();
  EXPECT_CALL(tpm_ownership_, GetTpmStatus).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, AddPcrValuesToPolicySession).Times(AtLeast(1));
  EXPECT_CALL(tpm_, CreatePrimarySyncShort).Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, NonEmptySealEmptyUnsealPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with some initial non-empty policy digest.
  policy_digest_ = DftPolicyDigest();
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with an empty policy.
  policy_ = ConstructEmptyPolicy();
  // Empty policy leads to a wrong policy digest.
  policy_digest_ = WrongPolicyDigest();
  sealed_storage_.reset_policy(policy_);
  EXPECT_CALL(tpm_ownership_, GetTpmStatus).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, AddPcrValuesToPolicySession).Times(0);
  EXPECT_CALL(tpm_, CreatePrimarySyncShort).Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, EmptySealNonEmptyUnsealPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructEmptyPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with initial empty policy.
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with some non-empty policy.
  policy_ = ConstructPcrBoundPolicy();
  // A non-empty policy leads to wrong policy digest.
  policy_digest_ = WrongPolicyDigest();
  sealed_storage_.reset_policy(policy_);
  EXPECT_CALL(tpm_ownership_, GetTpmStatus).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, AddPcrValuesToPolicySession).Times(1);
  EXPECT_CALL(tpm_, CreatePrimarySyncShort).Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, CanUnsealV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);
  ExpectCommandSequence();

  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set the correct expected plaintext size and unseal the V1 blob.
  sealed_storage_.set_plain_size_for_v1(data_to_seal.size());
  auto result = sealed_storage_.Unseal(sealed_data.value());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), data_to_seal);
}

TEST_F(SealedStorageTest, WrongSizeForV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);
  ExpectCommandSequence();

  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set a wrong expected plaintext size and try unsealing the V1 blob.
  sealed_storage_.set_plain_size_for_v1(data_to_seal.size() + 10);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  ASSERT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, WrongPolicySecret) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructSecretBoundPolicy("correct secret");
  sealed_storage_.reset_policy(policy_);

  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  policy_ = ConstructSecretBoundPolicy("wrong secret");
  sealed_storage_.reset_policy(policy_);
  // A different secret leads to a wrong policy digest.
  policy_digest_ = WrongPolicyDigest();
  EXPECT_CALL(tpm_ownership_, GetTpmStatus).Times(AtLeast(0));
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, PcrAndSecretWrongDeviceStateCorrectSecret) {
  policy_ = ConstructSecretAndPcrBoundPolicy("secret");
  policy_pcr_result_ = trunks::TPM_RC_VALUE;
  SetUpMockCalls();
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, PcrAndSecretCorrectDeviceStateWrongSecret) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructSecretAndPcrBoundPolicy("correct secret");
  sealed_storage_.reset_policy(policy_);

  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  policy_ = ConstructSecretBoundPolicy("wrong secret");
  sealed_storage_.reset_policy(policy_);
  // A different secret leads to a wrong policy digest.
  policy_digest_ = WrongPolicyDigest();
  EXPECT_CALL(tpm_ownership_, GetTpmStatus).Times(AtLeast(0));
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

}  // namespace sealed_storage

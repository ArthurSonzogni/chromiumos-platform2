// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/resource_manager.h>
#include <trunks/tpm_generated.h>
#include <trunks/tpm_simulator_handle.h>
#include <trunks/trunks_factory_impl.h>

#include "sealed_storage/sealed_storage.h"

namespace sealed_storage {
namespace {

constexpr size_t kPolicyDigestSize = 32; /* SHA-256 digest */
constexpr size_t kPcrValueSize = 32;     /* SHA-256 digest */
constexpr char kSecret[] = "secret";
constexpr char kWrongSecret[] = "wrong-secret";
constexpr char kOwnerPassword[] = "owner-password";
constexpr char kEndorsementPassword[] = "endorsement_password";
constexpr char kLockoutPassword[] = "lockout-password";

std::string ConstructPcrValue(uint8_t pcr) {
  return std::string(kPcrValueSize, pcr ^ 0xFF);
}

// Constructs a policy bound to pcr values that cannot be fulfilled.
Policy ConstructArbitraryPcrBoundPolicy() {
  Policy::PcrMap pcr_map;

  for (uint8_t pcr = 0; pcr < 10; pcr++) {
    pcr_map.emplace(pcr, ConstructPcrValue(pcr));
  }

  return {.pcr_map = pcr_map};
}

SecretData DftDataToSeal() {
  return SecretData("testdata");
}

Policy ConstructEmptyPolicy() {
  return {};
}

Policy ConstructSecretBoundPolicy(const std::string& secret) {
  return {.secret = SecretData(secret)};
}

// Convert the sealed data blob of the default version produced by Seal()
// into V1 blob.
void ConvertToV1(Data* sealed_data) {
  constexpr size_t kAdditionalV2DataSize =
      /* plain size */ sizeof(uint16_t) +
      /* policy digest */ sizeof(uint16_t) + kPolicyDigestSize;
  ASSERT_GE(sealed_data->size(), kAdditionalV2DataSize + 1);
  (*sealed_data)[0] = 0x01;
  sealed_data->erase(sealed_data->cbegin() + 1,
                     sealed_data->cbegin() + 1 + kAdditionalV2DataSize);
}

}  // namespace

// Tests using a TPM simulator.
class SealedStorageSimulatorTest : public ::testing::Test {
 public:
  SealedStorageSimulatorTest() = default;

  SealedStorageSimulatorTest(const SealedStorageSimulatorTest&) = delete;
  SealedStorageSimulatorTest& operator=(const SealedStorageSimulatorTest&) =
      delete;

  virtual ~SealedStorageSimulatorTest() = default;

  void SetUp() override {
    ASSERT_TRUE(tmp_tpm_dir_.CreateUniqueTempDir());

    low_level_transceiver_ = std::make_unique<trunks::TpmSimulatorHandle>(
        tmp_tpm_dir_.GetPath().value());
    ASSERT_TRUE(low_level_transceiver_->Init());
    low_level_factory_ = std::make_unique<trunks::TrunksFactoryImpl>(
        low_level_transceiver_.get());
    ASSERT_TRUE(low_level_factory_->Initialize());
    resource_manager_ = std::make_unique<trunks::ResourceManager>(
        *low_level_factory_, low_level_transceiver_.get());
    resource_manager_->Initialize();
    trunks_factory_ =
        std::make_unique<trunks::TrunksFactoryImpl>(resource_manager_.get());
    ASSERT_TRUE(trunks_factory_->Initialize());

    // The TPM simulator is kept in process. Clear in case a previous test
    // didn't do that.
    auto tpm_state = trunks_factory_->GetTpmState();
    trunks::TPM_RC result = tpm_state->Initialize();
    ASSERT_EQ(result, trunks::TPM_RC_SUCCESS);
    if (tpm_state->IsOwned()) {
      trunks_factory_->GetTpmUtility()->Clear();
    }

    // Take TPM ownership.
    result = trunks_factory_->GetTpmUtility()->PrepareForOwnership();
    ASSERT_EQ(result, trunks::TPM_RC_SUCCESS);

    result = trunks_factory_->GetTpmUtility()->TakeOwnership(
        kOwnerPassword, kEndorsementPassword, kLockoutPassword);
    ASSERT_EQ(result, trunks::TPM_RC_SUCCESS);

    sealed_storage_ = std::make_unique<SealedStorage>(
        policy_, trunks_factory_.get(), &tpm_ownership_);

    ON_CALL(tpm_ownership_, GetTpmStatus)
        .WillByDefault(testing::Invoke(
            [this](const tpm_manager::GetTpmStatusRequest& request,
                   tpm_manager::GetTpmStatusReply* reply, brillo::ErrorPtr*,
                   int) {
              reply->set_status(tpm_manager_result_);
              reply->mutable_local_data()->set_endorsement_password(
                  kEndorsementPassword);
              return true;
            }));
  }

  void TearDown() override {
    // The TPM simulator is kept in-process. Clear after usage.
    trunks_factory_->GetTpmUtility()->Clear();
  }

  Policy ConstructCurrentPcrBoundPolicy() const {
    Policy::PcrMap pcr_map;

    for (uint8_t pcr = 0; pcr < 10; pcr++) {
      std::string pcr_value;
      trunks_factory_->GetTpmUtility()->ReadPCR(pcr, &pcr_value);
      pcr_map.emplace(pcr, pcr_value);
    }

    return {.pcr_map = pcr_map};
  }

  Policy ConstructSecretAndPcrBoundPolicy(std::string secret) {
    Policy pcr_bound_policy = ConstructCurrentPcrBoundPolicy();
    Policy secret_bound_policy = ConstructSecretBoundPolicy(secret);
    return {.pcr_map = pcr_bound_policy.pcr_map,
            .secret = secret_bound_policy.secret};
  }

  void SealUnseal(bool expect_seal_success,
                  bool expect_unseal_success,
                  const SecretData& data_to_seal) {
    sealed_storage_->reset_policy(policy_);
    auto sealed_data = sealed_storage_->Seal(data_to_seal);
    EXPECT_EQ(sealed_data.has_value(), expect_seal_success);
    if (!sealed_data.has_value()) {
      return;
    }

    auto unsealed = sealed_storage_->Unseal(sealed_data.value());
    EXPECT_EQ(unsealed.has_value(), expect_unseal_success);
    if (expect_unseal_success && unsealed.has_value()) {
      EXPECT_EQ(unsealed.value(), data_to_seal);
    }
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};
  base::ScopedTempDir tmp_tpm_dir_;
  Policy policy_;
  std::unique_ptr<trunks::TpmSimulatorHandle> low_level_transceiver_;
  std::unique_ptr<trunks::TrunksFactoryImpl> low_level_factory_;
  std::unique_ptr<trunks::ResourceManager> resource_manager_;
  std::unique_ptr<trunks::TrunksFactoryImpl> trunks_factory_;
  testing::NiceMock<org::chromium::TpmManagerProxyMock> tpm_ownership_;
  std::unique_ptr<SealedStorage> sealed_storage_;

  tpm_manager::TpmManagerStatus tpm_manager_result_ =
      tpm_manager::STATUS_SUCCESS;
};

TEST_F(SealedStorageSimulatorTest, TrivialPolicySuccess) {
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageSimulatorTest, VariousPlaintextSizesSuccess) {
  for (size_t data_size = 0; data_size <= 65; data_size++) {
    std::string data(data_size, 'x');
    SealUnseal(true, true, SecretData(data));
  }
}

TEST_F(SealedStorageSimulatorTest, PcrBoundPolicySuccess) {
  policy_ = ConstructCurrentPcrBoundPolicy();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageSimulatorTest, SecretBoundPolicySuccess) {
  policy_ = ConstructSecretBoundPolicy(kSecret);
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageSimulatorTest, SecretAndPcrBoundPolicySuccess) {
  policy_ = ConstructSecretAndPcrBoundPolicy(kSecret);
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageSimulatorTest, PcrChangeOnUnsealError) {
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  const auto data_to_seal = DftDataToSeal();
  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  trunks::TPM_RC result =
      trunks_factory_->GetTpmUtility()->ExtendPCR(0, "extend", nullptr);
  ASSERT_EQ(result, trunks::TPM_RC_SUCCESS);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, CanUnsealV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set the correct expected plaintext size and unseal the V1 blob.
  sealed_storage_->set_plain_size_for_v1(data_to_seal.size());
  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  ASSERT_TRUE(unsealed.has_value());
  EXPECT_EQ(unsealed.value(), data_to_seal);
}

TEST_F(SealedStorageSimulatorTest, WrongSizeForV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set a wrong expected plaintext size and try unsealing the V1 blob.
  sealed_storage_->set_plain_size_for_v1(data_to_seal.size() + 10);
  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, WrongPolicySecret) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructSecretBoundPolicy(kSecret);
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  policy_ = ConstructSecretBoundPolicy(kWrongSecret);
  sealed_storage_->reset_policy(policy_);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, WrongPcrPolicy) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructArbitraryPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, PcrAndSecretWrongDeviceStateCorrectSecret) {
  policy_ = ConstructSecretAndPcrBoundPolicy(kSecret);
  sealed_storage_->reset_policy(policy_);

  const auto data_to_seal = DftDataToSeal();
  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  trunks::TPM_RC result =
      trunks_factory_->GetTpmUtility()->ExtendPCR(0, "extend", nullptr);
  ASSERT_EQ(result, trunks::TPM_RC_SUCCESS);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, PcrAndSecretCorrectDeviceStateWrongSecret) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructSecretAndPcrBoundPolicy(kSecret);
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  policy_ = ConstructSecretBoundPolicy(kWrongSecret);
  sealed_storage_->reset_policy(policy_);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, WrongPolicy) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  // Try unsealing with a different policy.
  policy_ = ConstructSecretBoundPolicy(kSecret);
  sealed_storage_->reset_policy(policy_);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, NonEmptySealEmptyUnsealPolicy) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  // Try unsealing with an empty policy.
  policy_ = ConstructEmptyPolicy();
  sealed_storage_->reset_policy(policy_);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

TEST_F(SealedStorageSimulatorTest, EmptySealNonEmptyUnsealPolicy) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructEmptyPolicy();
  sealed_storage_->reset_policy(policy_);

  // Set up sealed_data with initial empty policy.
  auto sealed_data = sealed_storage_->Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());

  // Try unsealing with some non-empty policy.
  policy_ = ConstructCurrentPcrBoundPolicy();
  sealed_storage_->reset_policy(policy_);

  auto unsealed = sealed_storage_->Unseal(sealed_data.value());
  EXPECT_FALSE(unsealed.has_value());
}

}  // namespace sealed_storage

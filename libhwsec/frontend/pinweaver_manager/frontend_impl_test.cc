// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functional tests for LECredentialManager + SignInHashTree.
#include <iterator>  // For std::begin()/std::end().
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <brillo/secure_blob.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest_prod.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "base/files/file_enumerator.h"
#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/frontend/pinweaver_manager/frontend_impl.h"
#include "libhwsec/proxy/tpm2_simulator_proxy_for_test.h"

using ::hwsec_foundation::GetSecureRandom;

namespace {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::IsOkAnd;
using ::hwsec_foundation::error::testing::IsOkAndHolds;
using ::hwsec_foundation::error::testing::NotOkAnd;
using ::testing::Eq;
using ::testing::Ge;

constexpr int kLEMaxIncorrectAttempt = 5;
constexpr uint8_t kAuthChannel = 0;

MATCHER_P(HasTPMRetryAction, matcher, "") {
  if (arg.ok()) {
    *result_listener << "status: " << arg.status();
    return false;
  }

  return ExplainMatchResult(matcher, arg->ToTPMRetryAction(), result_listener);
}

// All the keys are 32 bytes long.
constexpr uint8_t kLeSecret1Array[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x02};

constexpr uint8_t kLeSecret2Array[] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10, 0x12};

constexpr uint8_t kHeSecret1Array[] = {
    0x00, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x00,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x00, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

constexpr uint8_t kResetSecret1Array[] = {
    0x00, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

constexpr uint8_t kClientNonceArray[] = {
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

constexpr char kCredDirName[] = "low_entropy_creds";

// As the point needs to be valid, the point is pre-generated.
constexpr char kClientEccPointXHex[] =
    "78D184E439FD4EC5BADC5431C8A6DD8EC039F945E7AD9DEDC5166BEF390E9AFD";
constexpr char kClientEccPointYHex[] =
    "4E411B61F1B48601ED3A218E4EE6075A3053130E6F25BBFF7FE08BB6D3EC6BF6";

}  // namespace

namespace hwsec {

using ResetType = LECredentialManager::ResetType;

class LECredentialManagerImplTest : public ::testing::Test {
 public:
  LECredentialManagerImplTest()
      : kLeSecret1(std::begin(kLeSecret1Array), std::end(kLeSecret1Array)),
        kLeSecret2(std::begin(kLeSecret2Array), std::end(kLeSecret2Array)),
        kHeSecret1(std::begin(kHeSecret1Array), std::end(kHeSecret1Array)),
        kResetSecret1(std::begin(kResetSecret1Array),
                      std::end(kResetSecret1Array)),
        kClientNonce(std::begin(kClientNonceArray),
                     std::end(kClientNonceArray)) {
    CHECK(temp_dir_.CreateUniqueTempDir());
  }

  void SetUp() override {
    proxy_ = std::make_unique<Tpm2SimulatorProxyForTest>();
    CHECK(proxy_->Init());
    InitLEManager();
  }

  void InitLEManager() {
    auto backend = std::make_unique<BackendTpm2>(
        *proxy_, MiddlewareDerivative{}, CredDirPath());
    backend_ = backend.get();

    middleware_owner_.reset();
    middleware_owner_ = std::make_unique<MiddlewareOwner>(
        std::move(backend), ThreadingMode::kCurrentThread);

    backend_->set_middleware_derivative_for_test(middleware_owner_->Derive());
    le_mgr_.reset();
    le_mgr_ = std::make_unique<LECredentialManagerFrontendImpl>(
        middleware_owner_->Derive());
  }

  // Returns location of on-disk hash tree directory.
  base::FilePath CredDirPath() {
    return temp_dir_.GetPath().Append(kCredDirName);
  }

  // Helper function to create a credential & then lock it out.
  // NOTE: Parameterize the secrets once you have more than 1
  // of them.
  uint64_t CreateLockedOutCredential() {
    std::map<uint32_t, uint32_t> delay_sched = {
        {kLEMaxIncorrectAttempt, UINT32_MAX},
    };

    uint64_t label = le_mgr_
                         ->InsertCredential(
                             std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt)
                         .AssertOk()
                         .value();
    brillo::SecureBlob he_secret;
    brillo::SecureBlob reset_secret;
    for (int i = 0; i < kLEMaxIncorrectAttempt; i++) {
      EXPECT_THAT(le_mgr_->CheckCredential(label, kHeSecret1),
                  NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kUserAuth))));
    }
    return label;
  }

  // Corrupts |path| by replacing file contents with random data.
  void CorruptFile(base::FilePath path) {
    int64_t file_size;
    ASSERT_TRUE(base::GetFileSize(path, &file_size));
    std::vector<uint8_t> random_data(file_size);
    GetSecureRandom(random_data.data(), file_size);
    ASSERT_EQ(file_size,
              base::WriteFile(path, reinterpret_cast<char*>(random_data.data()),
                              file_size));
  }

  void CorruptLeafCache() {
    // Fill the leafcache file with random data.
    base::FilePath leaf_cache = CredDirPath().Append(kLeafCacheFileName);
    CorruptFile(leaf_cache);
  }

  // Corrupts all versions of the |label| leaf. We corrupt all the versions,
  // since it is tedious to find which is the most recent one.
  void CorruptHashTreeWithLabel(uint64_t label) {
    base::FilePath leaf_dir = CredDirPath().Append(std::to_string(label));
    ASSERT_TRUE(base::PathExists(leaf_dir));
    ASSERT_FALSE(leaf_dir.empty());

    base::FileEnumerator files(leaf_dir, false, base::FileEnumerator::FILES);
    for (base::FilePath cur_file = files.Next(); !cur_file.empty();
         cur_file = files.Next()) {
      CorruptFile(cur_file);
    }
  }

  // Takes a snapshot of the on-disk hash three, and returns the directory
  // where the snapshot is stored.
  std::unique_ptr<base::ScopedTempDir> CaptureSnapshot() {
    auto snapshot = std::make_unique<base::ScopedTempDir>();
    CHECK(snapshot->CreateUniqueTempDir());
    base::CopyDirectory(CredDirPath(), snapshot->GetPath(), true);

    return snapshot;
  }

  // Fills the on-disk hash tree with the contents of |snapshot_path|.
  void RestoreSnapshot(base::FilePath snapshot_path) {
    ASSERT_TRUE(brillo::DeletePathRecursively(CredDirPath()));
    ASSERT_TRUE(base::CopyDirectory(snapshot_path.Append(kCredDirName),
                                    temp_dir_.GetPath(), true));
  }

  void GeneratePk(uint8_t auth_channel) {
    hwsec::PinWeaverFrontend::PinWeaverEccPoint pt;
    brillo::Blob x_blob, y_blob;
    base::HexStringToBytes(kClientEccPointXHex, &x_blob);
    base::HexStringToBytes(kClientEccPointYHex, &y_blob);
    memcpy(pt.x, x_blob.data(), sizeof(pt.x));
    memcpy(pt.y, y_blob.data(), sizeof(pt.y));
    EXPECT_TRUE(backend_->GetPinWeaverTpm2().GeneratePk(auth_channel, pt).ok());
  }

  // Helper function to create a rate-limiter & then lock it out.
  uint64_t CreateLockedOutRateLimiter(uint8_t auth_channel) {
    const std::map<uint32_t, uint32_t> delay_sched = {
        {kLEMaxIncorrectAttempt, UINT32_MAX},
    };

    uint64_t label =
        le_mgr_
            ->InsertRateLimiter(auth_channel,
                                std::vector<hwsec::OperationPolicySetting>(),
                                kResetSecret1, delay_sched,
                                /*expiration_delay=*/std::nullopt)
            .AssertOk()
            .value();

    for (int i = 0; i < kLEMaxIncorrectAttempt; i++) {
      EXPECT_THAT(
          le_mgr_->StartBiometricsAuth(auth_channel, label, kClientNonce),
          IsOk());
    }
    return label;
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<Tpm2SimulatorProxyForTest> proxy_;
  std::unique_ptr<MiddlewareOwner> middleware_owner_;
  BackendTpm2* backend_;
  std::unique_ptr<LECredentialManagerFrontend> le_mgr_;
  const brillo::SecureBlob kLeSecret1;
  const brillo::SecureBlob kLeSecret2;
  const brillo::SecureBlob kHeSecret1;
  const brillo::SecureBlob kResetSecret1;
  const brillo::Blob kClientNonce;
};

// Basic check: Insert 2 labels, then verify we can retrieve them correctly.
// Here, we don't bother with specifying a delay schedule, we just want
// to check whether a simple Insert and Check works.
TEST_F(LECredentialManagerImplTest, BasicInsertAndCheck) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };

  uint64_t label1 =
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();
  uint64_t label2 =
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();

  auto result = le_mgr_->CheckCredential(label1, kLeSecret1);
  ASSERT_OK(result);
  EXPECT_EQ(result->he_secret, kHeSecret1);
  EXPECT_THAT(le_mgr_->CheckCredential(label2, kLeSecret1),
              NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kUserAuth))));
  result = le_mgr_->CheckCredential(label2, kLeSecret2);
  ASSERT_OK(result);
  EXPECT_EQ(result->he_secret, kHeSecret1);
}

// Basic check: Insert 2 rate-limiters, then verify we can retrieve them
// correctly.
TEST_F(LECredentialManagerImplTest, BiometricsBasicInsertAndCheck) {
  constexpr uint8_t kWrongAuthChannel = 1;
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  GeneratePk(kAuthChannel);
  uint64_t label1 =
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();
  uint64_t label2 =
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();
  auto reply1 =
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce);
  ASSERT_OK(reply1);

  auto reply2 =
      le_mgr_->StartBiometricsAuth(kAuthChannel, label2, kClientNonce);
  ASSERT_OK(reply2);
  // Server should return different values every time.
  EXPECT_NE(reply1->server_nonce, reply2->server_nonce);
  EXPECT_NE(reply1->iv, reply2->iv);
  EXPECT_NE(reply1->encrypted_he_secret, reply2->encrypted_he_secret);

  // Incorrect auth channel passed should result in INVALID_LE_SECRET.
  GeneratePk(kWrongAuthChannel);
  EXPECT_THAT(
      le_mgr_->StartBiometricsAuth(kWrongAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kUserAuth))));
}

// Verify invalid secrets and getting locked out due to too many attempts.
TEST_F(LECredentialManagerImplTest, LockedOutSecret) {
  uint64_t label1 = CreateLockedOutCredential();
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;

  EXPECT_THAT(
      le_mgr_->CheckCredential(label1, kLeSecret1),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  // Check once more to ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the
  // right metadata is stored.
  EXPECT_THAT(
      le_mgr_->CheckCredential(label1, kLeSecret1),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));
}

// Verify getting locked out due to too many attempts for biometrics
// rate-limiters.
TEST_F(LECredentialManagerImplTest, BiometricsLockedOutRateLimiter) {
  const brillo::Blob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));

  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);
  EXPECT_THAT(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  // Check once more to ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the
  // right metadata is stored.
  EXPECT_THAT(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));
}

// TODO(b/283182607): Add rate-limiter expiration tests after we can
// fast-forward time in TPM simulator.

// Insert a label. Then ensure that a CheckCredential on another non-existent
// label fails.
TEST_F(LECredentialManagerImplTest, InvalidLabelCheck) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  uint64_t label1 =
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();
  // First try a badly encoded label.
  uint64_t invalid_label = ~label1;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_THAT(le_mgr_->CheckCredential(invalid_label, kLeSecret1),
              NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kSpaceNotFound))));
  // Next check a valid, but absent label.
  invalid_label = label1 ^ 0x1;
  EXPECT_THAT(le_mgr_->CheckCredential(invalid_label, kLeSecret1),
              NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kSpaceNotFound))));
}

// Insert a credential and then remove it.
// Check that a subsequent CheckCredential on that label fails.
TEST_F(LECredentialManagerImplTest, BasicInsertRemove) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  uint64_t label1 =
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt)
          .AssertOk()
          .value();
  ASSERT_THAT(le_mgr_->RemoveCredential(label1), IsOk());
  EXPECT_THAT(le_mgr_->CheckCredential(label1, kLeSecret1),
              NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kSpaceNotFound))));
}

// Check that a reset unlocks a locked out credential.
TEST_F(LECredentialManagerImplTest, ResetSecret) {
  uint64_t label1 = CreateLockedOutCredential();

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_THAT(
      le_mgr_->CheckCredential(label1, kLeSecret1),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  EXPECT_THAT(le_mgr_->ResetCredential(label1, kResetSecret1,
                                       ResetType::kWrongAttempts),
              IsOk());

  // Make sure we can Check successfully, post reset.
  auto result = le_mgr_->CheckCredential(label1, kLeSecret1);
  ASSERT_OK(result);
  EXPECT_EQ(result->he_secret, kHeSecret1);
}

// Check that an invalid reset doesn't unlock a locked credential.
TEST_F(LECredentialManagerImplTest, ResetSecretNegative) {
  uint64_t label1 = CreateLockedOutCredential();
  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_THAT(
      le_mgr_->CheckCredential(label1, kLeSecret1),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  EXPECT_THAT(
      le_mgr_->ResetCredential(label1, kLeSecret1, ResetType::kWrongAttempts),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kUserAuth))));

  // Make sure that Check still fails.
  EXPECT_THAT(
      le_mgr_->CheckCredential(label1, kLeSecret1),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));
}

// Check that a reset unlocks a locked out rate-limiter.
TEST_F(LECredentialManagerImplTest, BiometricsResetSecret) {
  const brillo::Blob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));
  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_THAT(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  EXPECT_THAT(le_mgr_->ResetCredential(label1, kResetSecret1,
                                       ResetType::kWrongAttempts),
              IsOk());

  // Make sure we can Check successfully, post reset.
  EXPECT_THAT(le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
              IsOk());
}

// Check that an invalid reset doesn't unlock a locked rate-limiter.
TEST_F(LECredentialManagerImplTest, BiometricsResetSecretNegative) {
  const brillo::Blob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));
  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_THAT(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));

  EXPECT_THAT(
      le_mgr_->ResetCredential(label1, kLeSecret1, ResetType::kWrongAttempts),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kUserAuth))));

  // Make sure that StartBiometricsAuth still fails.
  EXPECT_THAT(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce),
      NotOkAnd(HasTPMRetryAction(Eq(TPMRetryAction::kPinWeaverLockedOut))));
}

}  // namespace hwsec

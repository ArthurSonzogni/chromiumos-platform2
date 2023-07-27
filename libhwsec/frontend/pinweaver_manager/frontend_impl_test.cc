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

}  // namespace

namespace hwsec {

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

}  // namespace hwsec

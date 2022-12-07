// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootlockbox/tpm_nvspace_impl.h"

#include <memory>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm_retry_action.h>
#include <libhwsec/frontend/bootlockbox/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {

// A helper function to serialize a uint16_t.
std::string uint16_to_string(uint16_t value) {
  const char* bytes = reinterpret_cast<const char*>(&value);
  return std::string(bytes, sizeof(uint16_t));
}

}  // namespace

namespace bootlockbox {

class TPMNVSpaceImplTest : public testing::Test {
 public:
  void SetUp() override {
    auto hwsec = std::make_unique<hwsec::MockBootLockboxFrontend>();
    hwsec_ptr_ = hwsec.get();
    nvspace_utility_ =
        std::make_unique<TPMNVSpaceImpl>(std::move(hwsec), &mock_tpm_owner_);
  }

 protected:
  hwsec::MockBootLockboxFrontend* hwsec_ptr_;
  NiceMock<org::chromium::TpmManagerProxyMock> mock_tpm_owner_;
  std::unique_ptr<TPMNVSpaceImpl> nvspace_utility_;
};

TEST_F(TPMNVSpaceImplTest, DefineNVSpaceSuccess) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(
          ReturnValue(hwsec::BootLockboxFrontend::StorageState::kPreparable));
  EXPECT_CALL(*hwsec_ptr_, PrepareSpace(kNVSpaceSize))
      .WillOnce(ReturnOk<hwsec::TPMError>());

  EXPECT_EQ(nvspace_utility_->DefineNVSpace(),
            NVSpaceState::kNVSpaceUninitialized);
}

TEST_F(TPMNVSpaceImplTest, DefineNVSpaceAlreadyDefined) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnValue(hwsec::BootLockboxFrontend::StorageState::kReady));

  EXPECT_EQ(nvspace_utility_->DefineNVSpace(),
            NVSpaceState::kNVSpaceUninitialized);
}

TEST_F(TPMNVSpaceImplTest, DefineNVSpaceCannotPrepare) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(
          ReturnValue(hwsec::BootLockboxFrontend::StorageState::kWriteLocked));

  EXPECT_EQ(nvspace_utility_->DefineNVSpace(), NVSpaceState::kNVSpaceError);
}

TEST_F(TPMNVSpaceImplTest, DefineNVSpacePrepareFail) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(
          ReturnValue(hwsec::BootLockboxFrontend::StorageState::kPreparable));
  EXPECT_CALL(*hwsec_ptr_, PrepareSpace(kNVSpaceSize))
      .WillOnce(ReturnError<hwsec::TPMError>("Fake error",
                                             hwsec::TPMRetryAction::kNoRetry));

  EXPECT_EQ(nvspace_utility_->DefineNVSpace(), NVSpaceState::kNVSpaceUndefined);
}

TEST_F(TPMNVSpaceImplTest, DefineNVSpacePowerWash) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnError<hwsec::TPMError>("Fake error",
                                             hwsec::TPMRetryAction::kNoRetry));

  EXPECT_EQ(nvspace_utility_->DefineNVSpace(),
            NVSpaceState::kNVSpaceNeedPowerwash);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceReboot) {
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnError<hwsec::TPMError>("Fake error",
                                             hwsec::TPMRetryAction::kNoRetry));

  std::string data;
  EXPECT_EQ(nvspace_utility_->ReadNVSpace(&data),
            NVSpaceState::kNVSpaceNeedPowerwash);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceLengthFail) {
  std::string nvram_data = uint16_to_string(1) /* version */ +
                           uint16_to_string(0) /* flags */ +
                           std::string(3, '\x3');
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnValue(hwsec::BootLockboxFrontend::StorageState::kReady));
  EXPECT_CALL(*hwsec_ptr_, LoadSpace())
      .WillOnce(ReturnValue(brillo::BlobFromString(nvram_data)));

  std::string data;
  EXPECT_EQ(nvspace_utility_->ReadNVSpace(&data), NVSpaceState::kNVSpaceError);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceUninitializedFail) {
  std::string nvram_data = std::string(kNVSpaceSize, '\0');
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnValue(hwsec::BootLockboxFrontend::StorageState::kReady));
  EXPECT_CALL(*hwsec_ptr_, LoadSpace())
      .WillOnce(ReturnValue(brillo::BlobFromString(nvram_data)));

  std::string data;
  EXPECT_EQ(nvspace_utility_->ReadNVSpace(&data),
            NVSpaceState::kNVSpaceUninitialized);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceVersionFail) {
  BootLockboxNVSpace space{.version = 2};
  std::string nvram_data =
      std::string(reinterpret_cast<char*>(&space), kNVSpaceSize);
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnValue(hwsec::BootLockboxFrontend::StorageState::kReady));
  EXPECT_CALL(*hwsec_ptr_, LoadSpace())
      .WillOnce(ReturnValue(brillo::BlobFromString(nvram_data)));

  std::string data;
  EXPECT_EQ(nvspace_utility_->ReadNVSpace(&data), NVSpaceState::kNVSpaceError);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceSuccess) {
  std::string test_digest(SHA256_DIGEST_LENGTH, 'a');
  BootLockboxNVSpace space{
      .version = 1,
      .flags = 0,
  };
  memcpy(space.digest, test_digest.c_str(), SHA256_DIGEST_LENGTH);
  std::string nvram_data =
      std::string(reinterpret_cast<char*>(&space), kNVSpaceSize);
  EXPECT_CALL(*hwsec_ptr_, GetSpaceState())
      .WillOnce(ReturnValue(hwsec::BootLockboxFrontend::StorageState::kReady));
  EXPECT_CALL(*hwsec_ptr_, LoadSpace())
      .WillOnce(ReturnValue(brillo::BlobFromString(nvram_data)));

  std::string data;
  EXPECT_EQ(nvspace_utility_->ReadNVSpace(&data), NVSpaceState::kNVSpaceNormal);
  EXPECT_EQ(data, test_digest);
}

TEST_F(TPMNVSpaceImplTest, WriteNVSpaceSuccess) {
  std::string nvram_data(SHA256_DIGEST_LENGTH, 'a');
  std::string data = uint16_to_string(1) /* version */ +
                     uint16_to_string(0) /* flags */ + nvram_data;
  EXPECT_CALL(*hwsec_ptr_, StoreSpace(brillo::BlobFromString(data)))
      .WillOnce(ReturnOk<hwsec::TPMError>());

  EXPECT_TRUE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPMNVSpaceImplTest, WriteNVSpaceInvalidLength) {
  std::string nvram_data = "data of invalid length";
  EXPECT_CALL(*hwsec_ptr_, StoreSpace(_)).Times(0);

  EXPECT_FALSE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPMNVSpaceImplTest, LockNVSpace) {
  EXPECT_CALL(*hwsec_ptr_, LockSpace()).WillOnce(ReturnOk<hwsec::TPMError>());

  EXPECT_TRUE(nvspace_utility_->LockNVSpace());
}

TEST_F(TPMNVSpaceImplTest, LockNVSpaceFail) {
  EXPECT_CALL(*hwsec_ptr_, LockSpace())
      .WillOnce(ReturnError<hwsec::TPMError>("Fake error",
                                             hwsec::TPMRetryAction::kNoRetry));

  EXPECT_FALSE(nvspace_utility_->LockNVSpace());
}
}  // namespace bootlockbox

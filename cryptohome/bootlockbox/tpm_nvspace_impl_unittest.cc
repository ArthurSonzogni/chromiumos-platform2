// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/tpm_nvspace_impl.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

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

uint32_t GetBootLockboxNVRamIndex() {
  TPM_SELECT_BEGIN;
  TPM1_SECTION({ return 0x20000006; });
  TPM2_SECTION({ return 0x800006; });
  OTHER_TPM_SECTION({
    LOG(ERROR) << "Failed to get the bootlockbox index on none supported TPM.";
    return 0;
  });
  TPM_SELECT_END;
}

}  // namespace

namespace cryptohome {

class TPMNVSpaceImplTest : public testing::Test {
 public:
  void SetUp() override {
    SET_DEFAULT_TPM_FOR_TESTING;
    nvspace_utility_ =
        std::make_unique<TPMNVSpaceImpl>(&mock_tpm_nvram_, &mock_tpm_owner_);
    nvspace_utility_->Initialize();
  }

 protected:
  NiceMock<org::chromium::TpmNvramProxyMock> mock_tpm_nvram_;
  NiceMock<org::chromium::TpmManagerProxyMock> mock_tpm_owner_;
  std::unique_ptr<TPMNVSpaceImpl> nvspace_utility_;
};

TEST_F(TPMNVSpaceImplTest, DefineNVSpaceSuccess) {
  EXPECT_CALL(mock_tpm_nvram_, DefineSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::DefineSpaceRequest& request,
                          tpm_manager::DefineSpaceReply* reply,
                          brillo::ErrorPtr*, int) {
        EXPECT_TRUE(request.has_index());
        EXPECT_EQ(GetBootLockboxNVRamIndex(), request.index());
        EXPECT_TRUE(request.has_size());
        EXPECT_EQ(kNVSpaceSize, request.size());
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        return true;
      }));
  EXPECT_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::RemoveOwnerDependencyRequest& request,
                    tpm_manager::RemoveOwnerDependencyReply* reply,
                    brillo::ErrorPtr*, int) {
            EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Bootlockbox,
                      request.owner_dependency());
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(true);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  EXPECT_TRUE(nvspace_utility_->DefineNVSpace());
}

TEST_F(TPMNVSpaceImplTest, DefineNVSpaceFail) {
  EXPECT_CALL(mock_tpm_nvram_, DefineSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::DefineSpaceRequest& request,
                          tpm_manager::DefineSpaceReply* reply,
                          brillo::ErrorPtr*, int) {
        reply->set_result(tpm_manager::NVRAM_RESULT_IPC_ERROR);
        return true;
      }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(true);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  EXPECT_FALSE(nvspace_utility_->DefineNVSpace());
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceLengthFail) {
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::ReadSpaceRequest& request,
                          tpm_manager::ReadSpaceReply* reply, brillo::ErrorPtr*,
                          int) {
        std::string nvram_data = uint16_to_string(1) /* version */ +
                                 uint16_to_string(0) /* flags */ +
                                 std::string(3, '\x3');
        // return success to trigger error.
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        reply->set_data(nvram_data);
        return true;
      }));
  std::string data;
  NVSpaceState state;
  EXPECT_FALSE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceError);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceUninitializedFail) {
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::ReadSpaceRequest& request,
                          tpm_manager::ReadSpaceReply* reply, brillo::ErrorPtr*,
                          int) {
        std::string nvram_data = std::string(kNVSpaceSize, '\0');
        // return success to trigger error.
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        reply->set_data(nvram_data);
        return true;
      }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(false);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_FALSE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceUninitialized);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceVersionFail) {
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::ReadSpaceRequest& request,
                          tpm_manager::ReadSpaceReply* reply, brillo::ErrorPtr*,
                          int) {
        BootLockboxNVSpace data;
        data.version = 2;
        std::string nvram_data =
            std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
        // return success to trigger error.
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        reply->set_data(nvram_data);
        return true;
      }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(false);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_FALSE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceError);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceSuccess) {
  std::string test_digest(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(
          Invoke([test_digest](const tpm_manager::ReadSpaceRequest& request,
                               tpm_manager::ReadSpaceReply* reply,
                               brillo::ErrorPtr*, int) {
            BootLockboxNVSpace data;
            data.version = 1;
            data.flags = 0;
            memcpy(data.digest, test_digest.c_str(), SHA256_DIGEST_LENGTH);
            std::string nvram_data =
                std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
            // return success to trigger error.
            reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
            reply->set_data(nvram_data);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(false);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_TRUE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceNormal);
  EXPECT_EQ(data, test_digest);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceClearOwnerPassSuccess) {
  std::string test_digest(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(
          Invoke([test_digest](const tpm_manager::ReadSpaceRequest& request,
                               tpm_manager::ReadSpaceReply* reply,
                               brillo::ErrorPtr*, int) {
            BootLockboxNVSpace data;
            data.version = 1;
            data.flags = 0;
            memcpy(data.digest, test_digest.c_str(), SHA256_DIGEST_LENGTH);
            std::string nvram_data =
                std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
            // return success to trigger error.
            reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
            reply->set_data(nvram_data);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(true);
            reply->set_is_owner_password_present(true);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::RemoveOwnerDependencyRequest& request,
                    tpm_manager::RemoveOwnerDependencyReply* reply,
                    brillo::ErrorPtr*, int) {
            EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Bootlockbox,
                      request.owner_dependency());
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  std::string data;
  NVSpaceState state;
  EXPECT_TRUE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceNormal);
  EXPECT_EQ(data, test_digest);
}

TEST_F(TPMNVSpaceImplTest, ReadNVSpaceClearOwnerPassNotOwnedSuccess) {
  std::string test_digest(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _, _, _))
      .WillOnce(
          Invoke([test_digest](const tpm_manager::ReadSpaceRequest& request,
                               tpm_manager::ReadSpaceReply* reply,
                               brillo::ErrorPtr*, int) {
            BootLockboxNVSpace data;
            data.version = 1;
            data.flags = 0;
            memcpy(data.digest, test_digest.c_str(), SHA256_DIGEST_LENGTH);
            std::string nvram_data =
                std::string(reinterpret_cast<char*>(&data), kNVSpaceSize);
            // return success to trigger error.
            reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
            reply->set_data(nvram_data);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::GetTpmNonsensitiveStatusRequest& request,
                    tpm_manager::GetTpmNonsensitiveStatusReply* reply,
                    brillo::ErrorPtr*, int) {
            reply->set_is_enabled(true);
            reply->set_is_owned(false);
            reply->set_is_owner_password_present(false);
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));
  EXPECT_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _, _, _))
      .WillOnce(
          Invoke([](const tpm_manager::RemoveOwnerDependencyRequest& request,
                    tpm_manager::RemoveOwnerDependencyReply* reply,
                    brillo::ErrorPtr*, int) {
            EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Bootlockbox,
                      request.owner_dependency());
            reply->set_status(tpm_manager::STATUS_SUCCESS);
            return true;
          }));

  EXPECT_CALL(mock_tpm_owner_,
              DoRegisterSignalOwnershipTakenSignalHandler(_, _))
      .WillOnce([](auto&& signal_callback, auto&& connect_callback) {
        std::move(*connect_callback).Run("", "", true);
        signal_callback.Run(tpm_manager::OwnershipTakenSignal());
      });
  std::string data;
  NVSpaceState state;
  EXPECT_TRUE(nvspace_utility_->ReadNVSpace(&data, &state));
  EXPECT_EQ(state, NVSpaceState::kNVSpaceNormal);
  EXPECT_EQ(data, test_digest);
}

TEST_F(TPMNVSpaceImplTest, WriteNVSpaceSuccess) {
  std::string nvram_data(SHA256_DIGEST_LENGTH, 'a');
  EXPECT_CALL(mock_tpm_nvram_, WriteSpace(_, _, _, _))
      .WillOnce(
          Invoke([nvram_data](const tpm_manager::WriteSpaceRequest& request,
                              tpm_manager::WriteSpaceReply* reply,
                              brillo::ErrorPtr*, int) {
            std::string data = uint16_to_string(1) /* version */ +
                               uint16_to_string(0) /* flags */ + nvram_data;
            EXPECT_EQ(data, request.data());
            EXPECT_FALSE(request.use_owner_authorization());
            reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
            return true;
          }));
  EXPECT_TRUE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPMNVSpaceImplTest, WriteNVSpaceInvalidLength) {
  std::string nvram_data = "data of invalid length";
  EXPECT_CALL(mock_tpm_nvram_, WriteSpace(_, _, _, _)).Times(0);
  EXPECT_FALSE(nvspace_utility_->WriteNVSpace(nvram_data));
}

TEST_F(TPMNVSpaceImplTest, LockNVSpace) {
  EXPECT_CALL(mock_tpm_nvram_, LockSpace(_, _, _, _))
      .WillOnce(Invoke([](const tpm_manager::LockSpaceRequest& request,
                          tpm_manager::LockSpaceReply* reply, brillo::ErrorPtr*,
                          int) {
        EXPECT_EQ(GetBootLockboxNVRamIndex(), request.index());
        EXPECT_FALSE(request.lock_read());
        EXPECT_TRUE(request.lock_write());
        EXPECT_FALSE(request.use_owner_authorization());
        reply->set_result(tpm_manager::NVRAM_RESULT_SUCCESS);
        return true;
      }));
  EXPECT_TRUE(nvspace_utility_->LockNVSpace());
}

}  // namespace cryptohome

// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_softclear_utils/tpm2_impl.h"

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_for_test.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAreArray;
using ::testing::NiceMock;
using ::testing::Return;

namespace tpm_softclear_utils {

class Tpm2ImplTest : public testing::Test {
 public:
  Tpm2ImplTest() = default;
  ~Tpm2ImplTest() override = default;

  void SetUp() override {
    trunks_factory_.set_tpm(&mock_tpm_);
    trunks_factory_.set_tpm_state(&mock_tpm_state_);
    trunks_factory_.set_used_password(&used_lockout_passwords_);

    ON_CALL(mock_tpm_state_, Initialize())
        .WillByDefault(Return(trunks::TPM_RC_SUCCESS));

    tpm2_impl_.set_trunks_factory(&trunks_factory_);
  }

 protected:
  NiceMock<libstorage::MockPlatform> mock_platform_;
  NiceMock<trunks::MockTpm> mock_tpm_;
  NiceMock<trunks::MockTpmState> mock_tpm_state_;
  trunks::TrunksFactoryForTest trunks_factory_;

  Tpm2Impl tpm2_impl_{&mock_platform_};
  std::vector<std::string> used_lockout_passwords_;
};

TEST_F(Tpm2ImplTest, GetLockoutPasswordFromFile) {
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).WillOnce(Return(true));

  std::string expected_lockout_password(kLockoutPasswordSize, '1');
  tpm_manager::LocalData local_data;
  local_data.set_lockout_password(expected_lockout_password);

  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(local_data.SerializeAsString()),
                      Return(true)));

  std::optional<std::string> actual_lockout_password =
      tpm2_impl_.GetAuthForOwnerReset();
  EXPECT_TRUE(actual_lockout_password);
  EXPECT_EQ(*actual_lockout_password, expected_lockout_password);
}

TEST_F(Tpm2ImplTest, GetDefaultLockoutPassword) {
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).WillOnce(Return(false));
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .Times(0);

  std::optional<std::string> actual_lockout_password =
      tpm2_impl_.GetAuthForOwnerReset();
  EXPECT_TRUE(actual_lockout_password);
  EXPECT_EQ(*actual_lockout_password, kDefaultLockoutPassword);
}

TEST_F(Tpm2ImplTest, GetLockoutPasswordUninitializedTrunksFactory) {
  tpm2_impl_.set_trunks_factory(nullptr);

  EXPECT_CALL(mock_tpm_state_, Initialize()).Times(0);
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .Times(0);

  EXPECT_FALSE(tpm2_impl_.GetAuthForOwnerReset());
}

TEST_F(Tpm2ImplTest, GetLockoutPasswordTpmStateError) {
  EXPECT_CALL(mock_tpm_state_, Refresh())
      .WillOnce(Return(trunks::TPM_RC_FAILURE));
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).Times(0);
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .Times(0);

  EXPECT_FALSE(tpm2_impl_.GetAuthForOwnerReset());
}

TEST_F(Tpm2ImplTest, GetLockoutPasswordReadFileError) {
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).WillOnce(Return(true));
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .WillOnce(Return(false));

  tpm_manager::LocalData local_data;
  std::string password(kLockoutPasswordSize, '1');
  local_data.set_lockout_password(password);

  EXPECT_FALSE(tpm2_impl_.GetAuthForOwnerReset());
}

TEST_F(Tpm2ImplTest, GetLockoutPasswordParseFileError) {
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).WillOnce(Return(true));
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .WillOnce(DoAll(SetArgPointee<1>("nonsense"), Return(true)));

  EXPECT_FALSE(tpm2_impl_.GetAuthForOwnerReset());
}

TEST_F(Tpm2ImplTest, GetLockoutPasswordBadPassword) {
  EXPECT_CALL(mock_tpm_state_, IsLockoutPasswordSet()).WillOnce(Return(true));

  tpm_manager::LocalData local_data;
  EXPECT_CALL(mock_platform_,
              ReadFileToString(base::FilePath(kTpmLocalDataFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(local_data.SerializeAsString()),
                      Return(true)));

  EXPECT_FALSE(tpm2_impl_.GetAuthForOwnerReset());
}

TEST_F(Tpm2ImplTest, ClearTpmSuccess) {
  std::string expected_handle_name;
  trunks::Serialize_TPM_HANDLE(trunks::TPM_RH_LOCKOUT, &expected_handle_name);

  EXPECT_CALL(mock_tpm_,
              ClearSync(trunks::TPM_RH_LOCKOUT, expected_handle_name, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  const std::string expected_password = "12345";
  EXPECT_TRUE(tpm2_impl_.SoftClearOwner(expected_password));
  EXPECT_THAT(used_lockout_passwords_, ElementsAreArray({expected_password}));
}

TEST_F(Tpm2ImplTest, ClearTpmUninitializedTrunksFactory) {
  tpm2_impl_.set_trunks_factory(nullptr);

  EXPECT_CALL(mock_tpm_, ClearSync(_, _, _)).Times(0);

  EXPECT_FALSE(tpm2_impl_.SoftClearOwner("12345"));
}

TEST_F(Tpm2ImplTest, ClearTpmFailure) {
  EXPECT_CALL(mock_tpm_, ClearSync(_, _, _))
      .WillOnce(Return(trunks::TPM_RC_FAILURE));

  EXPECT_FALSE(tpm2_impl_.SoftClearOwner("12345"));
}

}  // namespace tpm_softclear_utils

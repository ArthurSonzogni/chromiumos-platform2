// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for InstallAttributes.

#include "cryptohome/install_attributes.h"

#include <string>
#include <vector>

#include <algorithm>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/lockbox.h"
#include "cryptohome/mock_lockbox.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using base::FilePath;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace cryptohome {

namespace {
static constexpr char kTestName[] = "Shuffle";
static constexpr char kTestData[] = "Duffle";
}  // namespace

// Provides a test fixture for ensuring Lockbox-flows work as expected.
//
// Multiple helpers are included to ensure tests are starting from the same
// baseline for difference scenarios, such as first boot or all-other-normal
// boots.
class InstallAttributesTest : public ::testing::Test {
 public:
  InstallAttributesTest() : install_attrs_(nullptr) {}
  InstallAttributesTest(const InstallAttributesTest&) = delete;
  InstallAttributesTest& operator=(const InstallAttributesTest&) = delete;

  ~InstallAttributesTest() override = default;

  void SetUp() override {
    ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));

    install_attrs_.set_lockbox(&lockbox_);
    install_attrs_.set_platform(&platform_);
    // No pre-existing data and no TPM auth.
    EXPECT_CALL(lockbox_, set_tpm(&tpm_)).Times(1);
    EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
    install_attrs_.SetTpm(&tpm_);
    Mock::VerifyAndClearExpectations(&lockbox_);
    Mock::VerifyAndClearExpectations(&tpm_);
  }

  void GetAndCheck() {
    EXPECT_EQ(1, install_attrs_.Count());
    brillo::Blob data;
    EXPECT_TRUE(install_attrs_.Get(kTestName, &data));
    std::string data_str(reinterpret_cast<const char*>(data.data()),
                         data.size());
    EXPECT_STREQ(data_str.c_str(), kTestData);
  }

  // Generate the data we'll need to load from.
  brillo::Blob GenerateTestDataFileContents() {
    brillo::Blob data;
    SerializedInstallAttributes proto;
    proto.set_version(proto.version());
    SerializedInstallAttributes::Attribute* attr = proto.add_attributes();
    attr->set_name(kTestName);
    attr->set_value(std::string(reinterpret_cast<const char*>(kTestData),
                                strlen(kTestData)));
    data.resize(proto.ByteSizeLong());
    CHECK(proto.SerializeWithCachedSizesToArray(data.data()));
    return data;
  }

  void ExpectRemovingOwnerDependency() {
    EXPECT_CALL(tpm_, RemoveOwnerDependency(
                          Tpm::TpmOwnerDependency::kInstallAttributes))
        .Times(1);
  }

  void ExpectNotRemovingOwnerDependency() {
    EXPECT_CALL(tpm_, RemoveOwnerDependency(_)).Times(0);
  }

  NiceMock<MockLockbox> lockbox_;
  brillo::Blob lockbox_data_;
  InstallAttributes install_attrs_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
};

TEST_F(InstallAttributesTest, OobeWithTpm) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  // The first Init() call finds no data file and an unowned TPM.
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(false));
  EXPECT_FALSE(install_attrs_.Init(&tpm_));
  Mock::VerifyAndClearExpectations(&tpm_);
  Mock::VerifyAndClearExpectations(&platform_);
  EXPECT_EQ(InstallAttributes::Status::kTpmNotOwned, install_attrs_.status());

  // After taking ownership, TPM is ready and Init creates the lockbox.
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(lockbox_, Reset(_)).WillOnce(Return(true));
  ExpectRemovingOwnerDependency();
  EXPECT_TRUE(install_attrs_.Init(&tpm_));
  Mock::VerifyAndClearExpectations(&lockbox_);
  Mock::VerifyAndClearExpectations(&platform_);
  EXPECT_EQ(InstallAttributes::Status::kFirstInstall, install_attrs_.status());

  // Set the test attribute.
  brillo::Blob data(kTestData, kTestData + sizeof(kTestData));
  data.assign(kTestData, kTestData + strlen(kTestData));
  EXPECT_TRUE(install_attrs_.Set(kTestName, data));

  // Finalize.
  EXPECT_CALL(lockbox_, Store(_, _)).WillOnce(Return(true));
  brillo::Blob serialized_data;
  EXPECT_CALL(platform_,
              WriteFileAtomicDurable(
                  FilePath(InstallAttributes::kDefaultDataFile), _, _))
      .WillOnce(DoAll(SaveArg<1>(&serialized_data), Return(true)));
  brillo::Blob cached_data;
  EXPECT_CALL(
      platform_,
      WriteFileAtomic(FilePath(InstallAttributes::kDefaultCacheFile), _, _))
      .WillOnce(DoAll(SaveArg<1>(&cached_data), Return(true)));

  EXPECT_TRUE(install_attrs_.Finalize());
  Mock::VerifyAndClearExpectations(&lockbox_);
  Mock::VerifyAndClearExpectations(&platform_);
  EXPECT_EQ(InstallAttributes::Status::kValid, install_attrs_.status());

  brillo::Blob expected_data = GenerateTestDataFileContents();
  EXPECT_EQ(expected_data, serialized_data);
  EXPECT_EQ(expected_data, cached_data);
}

TEST_F(InstallAttributesTest, OobeWithoutTpm) {
  EXPECT_CALL(lockbox_, set_tpm(nullptr)).Times(1);
  install_attrs_.SetTpm(nullptr);

  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_FALSE(install_attrs_.is_secure());

  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(Return(false));
  ExpectNotRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall, install_attrs_.status());
}

TEST_F(InstallAttributesTest, OobeWithTpmBadWrite) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  // Assume authorization and working tpm.
  EXPECT_CALL(lockbox_, tpm()).WillRepeatedly(Return(&tpm_));
  EXPECT_CALL(lockbox_, Reset(_)).WillOnce(Return(true));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));
  Mock::VerifyAndClearExpectations(&lockbox_);

  brillo::Blob data;
  data.assign(kTestData, kTestData + strlen(kTestData));
  EXPECT_TRUE(install_attrs_.Set(kTestName, data));

  EXPECT_CALL(lockbox_, Store(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(_, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(install_attrs_.Finalize());

  EXPECT_EQ(InstallAttributes::Status::kInvalid, install_attrs_.status());
}

TEST_F(InstallAttributesTest, NormalBootWithTpm) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  brillo::Blob serialized_data = GenerateTestDataFileContents();
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(serialized_data), Return(true)));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kValid, install_attrs_.status());

  // Make sure the data was parsed correctly.
  GetAndCheck();
}

TEST_F(InstallAttributesTest, NormalBootWithoutTpm) {
  EXPECT_CALL(lockbox_, set_tpm(nullptr)).Times(1);
  install_attrs_.SetTpm(nullptr);

  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_FALSE(install_attrs_.is_secure());

  brillo::Blob serialized_data = GenerateTestDataFileContents();
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(serialized_data), Return(true)));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kValid, install_attrs_.status());

  // Make sure the data was parsed correctly.
  GetAndCheck();
}

// Represents that the OOBE process was interrupted by a reboot or crash prior
// to Finalize() being called, but after the Lockbox was reset.
// Since InstallAttributes Set/Finalize is not atomic, there is always the risk
// of data loss due to failure of the device. It will fail-safe however (by
// failing empty).
TEST_F(InstallAttributesTest, NormalBootUnlocked) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(Return(false));
  EXPECT_CALL(lockbox_, Reset(_)).WillOnce(Return(true));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

// Represents that the OOBE process was interrupted by a reboot or crash prior
// to Finalize() being called, and before the Lockbox was Created.
TEST_F(InstallAttributesTest, NormalBootNoSpace) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(lockbox_, Reset(_)).WillOnce(Return(true));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kFirstInstall, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

TEST_F(InstallAttributesTest, NormalBootReadFileError) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(Return(false));
  EXPECT_CALL(lockbox_, Reset(_))
      .WillOnce(
          DoAll(SetArgPointee<0>(LockboxError::kNvramInvalid), Return(false)));
  ExpectNotRemovingOwnerDependency();
  EXPECT_CALL(platform_, DeleteFile(_)).Times(0);
  EXPECT_CALL(platform_, DeletePathRecursively(_)).Times(0);

  EXPECT_FALSE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kInvalid, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

TEST_F(InstallAttributesTest, LegacyBoot) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(Return(false));
  EXPECT_CALL(lockbox_, Reset(_))
      .WillOnce(DoAll(SetArgPointee<0>(LockboxError::kNvramSpaceAbsent),
                      Return(false)));
  ExpectRemovingOwnerDependency();

  EXPECT_TRUE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kValid, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

// If the Lockbox Reset fails for reasons other than bad password, it should
// still be treated as if locked without any attributes set.
TEST_F(InstallAttributesTest, LegacyBootUnexpected) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillOnce(Return(false));
  EXPECT_CALL(lockbox_, Reset(_))
      .WillOnce(
          DoAll(SetArgPointee<0>(LockboxError::kTpmError), Return(false)));

  EXPECT_FALSE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kInvalid, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

// If initializing with an unowned TPM, the old data file should be deleted to
// make sure that we don't accidentally pick it up as valid after taking
// ownership.
TEST_F(InstallAttributesTest, ClearPreviousDataFile) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(false));

  // The cache file isn't present because lockbox-cache won't receive a dump of
  // the lockbox space if the TPM isn't owned.
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              FileExists(FilePath(InstallAttributes::kDefaultDataFile)))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              DeleteFile(FilePath(InstallAttributes::kDefaultDataFile)))
      .WillOnce(Return(true));

  EXPECT_FALSE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kTpmNotOwned, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

// Check that if the TPM is out for lunch and inoperable in this boot cycle, we
// do keep around the data file as to not irrevocably invalidate install
// attributes should the TPM start functioning again after reboot.
TEST_F(InstallAttributesTest, KeepDataFileOnTpmFailure) {
  EXPECT_EQ(InstallAttributes::Status::kUnknown, install_attrs_.status());
  EXPECT_TRUE(install_attrs_.is_secure());

  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(false));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(false));

  // The cache file isn't present because lockbox-cache won't receive a dump of
  // the lockbox space if the TPM isn't owned.
  EXPECT_CALL(platform_,
              ReadFile(FilePath(InstallAttributes::kDefaultCacheFile), _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              FileExists(FilePath(InstallAttributes::kDefaultDataFile)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DeleteFile(FilePath(InstallAttributes::kDefaultDataFile)))
      .Times(0);
  EXPECT_CALL(platform_, DeletePathRecursively(
                             FilePath(InstallAttributes::kDefaultDataFile)))
      .Times(0);

  EXPECT_FALSE(install_attrs_.Init(&tpm_));

  EXPECT_EQ(InstallAttributes::Status::kInvalid, install_attrs_.status());
  EXPECT_EQ(0, install_attrs_.Count());
}

}  // namespace cryptohome

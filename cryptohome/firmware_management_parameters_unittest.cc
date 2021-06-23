// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for FirmwareManagementParameters.

#include "cryptohome/firmware_management_parameters.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_firmware_management_parameters.h"
#include "cryptohome/mock_fwmp_checker.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

extern "C" {
#include "cryptohome/crc8.h"
}

namespace cryptohome {
using brillo::SecureBlob;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

// Provides a test fixture for ensuring Firmware Management Parameters
// flows work as expected.
//
// Multiple helpers are included to ensure tests are starting from the same
// baseline for difference scenarios, such as first boot or all-other-normal
// boots.
template <
    FirmwareManagementParameters::ResetMethod reset_method,
    FirmwareManagementParameters::WriteProtectionMethod write_protection_method>
class FirmwareManagementParametersTestBase : public ::testing::Test {
 public:
  FirmwareManagementParametersTestBase()
      : fwmp_checker_(new MockFwmpChecker()),
        fwmp_(reset_method,
              write_protection_method,
              &tpm_,
              std::unique_ptr<FwmpChecker>(fwmp_checker_)) {}
  FirmwareManagementParametersTestBase(
      const FirmwareManagementParametersTestBase&) = delete;
  FirmwareManagementParametersTestBase& operator=(
      const FirmwareManagementParametersTestBase&) = delete;

  virtual ~FirmwareManagementParametersTestBase() {}

  virtual void SetUp() {
    // Create the OOBE data to reuse for post-boot tests.
    fwmp_flags_ = 0x1234;
    fwmp_hash_.assign(kHashData, kHashData + strlen(kHashData));
    fwmp_hash_ptr_ = &fwmp_hash_;
  }

  // Sets the expectations for `FirmwareManagementParameters::Store()` according
  // to the configurations.
  void SetExpectationForStore(SecureBlob* nvram_data) {
    // Ensure an enabled, owned TPM.
    EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
    EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));

    InSequence s;
    EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
        .WillOnce(Return(true));
    EXPECT_CALL(tpm_, IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
        .WillOnce(Return(false));
    EXPECT_CALL(tpm_, GetNvramSize(FirmwareManagementParameters::kNvramIndex))
        .WillOnce(Return(FirmwareManagementParameters::kNvramBytes));
    EXPECT_CALL(*fwmp_checker_,
                IsValidForWrite(FirmwareManagementParameters::kNvramIndex))
        .WillOnce(Return(true));

    // Save blob that was written
    if (write_protection_method ==
        FirmwareManagementParameters::WriteProtectionMethod::
            kOwnerAuthorization) {
      EXPECT_CALL(tpm_,
                  OwnerWriteNvram(FirmwareManagementParameters::kNvramIndex, _))
          .WillOnce(DoAll(SaveArg<1>(nvram_data), Return(true)));
    } else {
      EXPECT_CALL(tpm_,
                  WriteNvram(FirmwareManagementParameters::kNvramIndex, _))
          .WillOnce(DoAll(SaveArg<1>(nvram_data), Return(true)));
    }

    if (write_protection_method ==
        FirmwareManagementParameters::WriteProtectionMethod::kWriteLock) {
      EXPECT_CALL(tpm_,
                  WriteLockNvram(FirmwareManagementParameters::kNvramIndex))
          .WillOnce(Return(true));
      EXPECT_CALL(tpm_,
                  IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
          .WillOnce(Return(true));
    }
  }

  const char* kHashData = "AxxxxxxxBxxxxxxxCxxxxxxxDxxxxxxE";
  const brillo::SecureBlob kContentsWithHash = {
      // clang-format off
    0xd2,
    0x28,
    0x10,
    0x00,
    0x34, 0x12, 0x00, 0x00,
    'A', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'B', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'C', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'D', 'x', 'x', 'x', 'x', 'x', 'x', 'E'
      // clang-format on
  };
  const brillo::SecureBlob kContentsNoHash = {
      // clang-format off
    0x6c,
    0x28,
    0x10,
    0x00,
    0x34, 0x12, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
      // clang-format on
  };
  MockFwmpChecker* fwmp_checker_;
  FirmwareManagementParameters fwmp_;
  NiceMock<MockTpm> tpm_;
  uint32_t fwmp_flags_;
  brillo::Blob fwmp_hash_;
  brillo::Blob* fwmp_hash_ptr_;
};

class FirmwareManagementParametersTest
    : public FirmwareManagementParametersTestBase<
          FirmwareManagementParameters::ResetMethod::kRecreateSpace,
          FirmwareManagementParameters::WriteProtectionMethod::kWriteLock> {};

// Create a new space
TEST_F(FirmwareManagementParametersTest, CreateNew) {
  // HasAuthorization() checks for Create() and Destroy()
  EXPECT_CALL(tpm_, IsEnabled()).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent())
      .Times(2)
      .WillRepeatedly(DoAll(Return(true)));

  // Destroy() doesn't find an existing space
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  // Create the new space
  EXPECT_CALL(tpm_, DefineNvram(FirmwareManagementParameters::kNvramIndex,
                                FirmwareManagementParameters::kNvramBytes,
                                Tpm::kTpmNvramWriteDefine |
                                    Tpm::kTpmNvramFirmwareReadable))
      .WillOnce(Return(true));
  EXPECT_TRUE(fwmp_.Create());
}

// Create on top of an existing space
TEST_F(FirmwareManagementParametersTest, CreateOverExisting) {
  // HasAuthorization() checks for Create() and Destroy()
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwnerPasswordPresent()).WillByDefault(DoAll(Return(true)));

  // Destroy() the existing space
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, DestroyNvram(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));

  // Create the new space
  EXPECT_CALL(tpm_, DefineNvram(FirmwareManagementParameters::kNvramIndex,
                                FirmwareManagementParameters::kNvramBytes,
                                Tpm::kTpmNvramWriteDefine |
                                    Tpm::kTpmNvramFirmwareReadable))
      .WillOnce(Return(true));
  EXPECT_TRUE(fwmp_.Create());
}

// Create fails without auth
TEST_F(FirmwareManagementParametersTest, CreateWithNoAuth) {
  // Enabled and owned succeed
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));

  // No password for you
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent()).WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Create());
}

// Create fails on define error
TEST_F(FirmwareManagementParametersTest, CreateDefineError) {
  // HasAuthorization() checks for Create() and Destroy()
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwnerPasswordPresent()).WillByDefault(DoAll(Return(true)));

  // Destroy() doesn't find an existing space
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  // Create the new space fails
  EXPECT_CALL(tpm_, DefineNvram(FirmwareManagementParameters::kNvramIndex,
                                FirmwareManagementParameters::kNvramBytes,
                                Tpm::kTpmNvramWriteDefine |
                                    Tpm::kTpmNvramFirmwareReadable))
      .WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Create());
}

// Destroy existing space
TEST_F(FirmwareManagementParametersTest, DestroyExisting) {
  // HasAuthorization() checks for Destroy()
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent()).WillOnce(DoAll(Return(true)));

  // Destroy() the existing space
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, DestroyNvram(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));

  EXPECT_TRUE(fwmp_.Destroy());
}

// Destroy non-existing space
TEST_F(FirmwareManagementParametersTest, DestroyNonExisting) {
  // HasAuthorization() checks for Destroy()
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent()).WillOnce(DoAll(Return(true)));

  // Destroy() non-existing space is fine
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  EXPECT_TRUE(fwmp_.Destroy());
}

// Destroy fails without auth
TEST_F(FirmwareManagementParametersTest, DestroyWithNoAuth) {
  // Enabled and owned succeed
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));

  // No password for you
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent()).WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Destroy());
}

// Destroy failure
TEST_F(FirmwareManagementParametersTest, DestroyFailure) {
  // HasAuthorization() checks for Destroy()
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwnerPasswordPresent()).WillOnce(DoAll(Return(true)));

  // Destroy() the existing space
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, DestroyNvram(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  EXPECT_FALSE(fwmp_.Destroy());
}

// Store flags and hash
TEST_F(FirmwareManagementParametersTest, StoreFlagsAndHash) {
  SecureBlob nvram_data;
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsWithHash);
}

// Store flags only
TEST_F(FirmwareManagementParametersTest, StoreFlagsOnly) {
  SecureBlob nvram_data;
  fwmp_hash_ptr_ = NULL;
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsNoHash);
}

// Store fails if TPM isn't ready
TEST_F(FirmwareManagementParametersTest, StoreNotReady) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
}

// Store fails if space doesn't exist
TEST_F(FirmwareManagementParametersTest, StoreNoNvram) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
}

// Store fails if space is locked
TEST_F(FirmwareManagementParametersTest, StoreLockedNvram) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
}

// Store fails if space is wrong size
TEST_F(FirmwareManagementParametersTest, StoreNvramSizeBad) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  // Return a bad NVRAM size.
  EXPECT_CALL(tpm_, GetNvramSize(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(4));
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
}

// Store fails if the space is not valid for write operation.
TEST_F(FirmwareManagementParametersTest, StoreNvramNotValidForWrite) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));

  // Return a bad NVRAM size.
  EXPECT_CALL(tpm_, GetNvramSize(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(FirmwareManagementParameters::kNvramBytes));
  EXPECT_CALL(*fwmp_checker_,
              IsValidForWrite(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
}

// Store fails if hash is wrong size
TEST_F(FirmwareManagementParametersTest, StoreHashSizeBad) {
  EXPECT_CALL(tpm_, IsEnabled()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(false));
  EXPECT_CALL(tpm_, GetNvramSize(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(FirmwareManagementParameters::kNvramBytes));

  // Return a bad NVRAM size.
  brillo::Blob bad_hash = brillo::BlobFromString("wrong-size");
  EXPECT_FALSE(fwmp_.Store(fwmp_flags_, &bad_hash));
  EXPECT_FALSE(fwmp_.IsLoaded());
}

// Load existing data
TEST_F(FirmwareManagementParametersTest, LoadExisting) {
  uint32_t flags;
  brillo::Blob hash;
  SecureBlob nvram_data(kContentsWithHash);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));

  // Load succeeds
  EXPECT_FALSE(fwmp_.IsLoaded());
  EXPECT_TRUE(fwmp_.Load());
  EXPECT_TRUE(fwmp_.IsLoaded());

  // And really loaded things
  EXPECT_TRUE(fwmp_.GetFlags(&flags));
  EXPECT_EQ(flags, fwmp_flags_);
  EXPECT_TRUE(fwmp_.GetDeveloperKeyHash(&hash));
  EXPECT_EQ(fwmp_hash_, hash);
}

// GetFlags automatically loads
TEST_F(FirmwareManagementParametersTest, GetFlags) {
  uint32_t flags;
  brillo::Blob hash;
  SecureBlob nvram_data(kContentsWithHash);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));

  EXPECT_FALSE(fwmp_.IsLoaded());
  EXPECT_TRUE(fwmp_.GetFlags(&flags));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(flags, fwmp_flags_);
}

// GetDeveloperKeyHash automatically loads
TEST_F(FirmwareManagementParametersTest, GetDeveloperKeyHash) {
  brillo::Blob hash;
  SecureBlob nvram_data(kContentsWithHash);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));

  EXPECT_FALSE(fwmp_.IsLoaded());
  EXPECT_TRUE(fwmp_.GetDeveloperKeyHash(&hash));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(fwmp_hash_, hash);
}

// Load and Get fail if space doesn't exist
TEST_F(FirmwareManagementParametersTest, LoadNoNvram) {
  uint32_t flags;
  brillo::Blob hash;

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .Times(3)
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(fwmp_.Load());
  EXPECT_FALSE(fwmp_.IsLoaded());

  EXPECT_FALSE(fwmp_.GetFlags(&flags));
  EXPECT_FALSE(fwmp_.IsLoaded());

  EXPECT_FALSE(fwmp_.GetDeveloperKeyHash(&hash));
  EXPECT_FALSE(fwmp_.IsLoaded());
}

// Load fails on read error
TEST_F(FirmwareManagementParametersTest, LoadReadError) {
  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(Return(false));
  EXPECT_FALSE(fwmp_.Load());
}

// Load fails on space too small
TEST_F(FirmwareManagementParametersTest, LoadNvramTooSmall) {
  SecureBlob nvram_data(kContentsWithHash);

  nvram_data.erase(nvram_data.begin(), nvram_data.begin() + 1);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));
  EXPECT_FALSE(fwmp_.Load());
}

// Load fails on bad struct size
TEST_F(FirmwareManagementParametersTest, LoadBadStructSize) {
  SecureBlob nvram_data(kContentsWithHash);

  // Alter struct size
  nvram_data[1]++;

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));
  EXPECT_FALSE(fwmp_.Load());
}

// Load fails on bad CRC
TEST_F(FirmwareManagementParametersTest, LoadBadCrc) {
  SecureBlob nvram_data(kContentsWithHash);

  // Alter CRC
  nvram_data[0] ^= 0x42;

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));
  EXPECT_FALSE(fwmp_.Load());
}

// Load allows different minor version
TEST_F(FirmwareManagementParametersTest, LoadMinorVersion) {
  SecureBlob nvram_data(kContentsWithHash);

  // Alter minor version
  nvram_data[2] += 1;

  // Recalculate CRC
  nvram_data[0] =
      crc8(nvram_data.data() + FirmwareManagementParameters::kCrcDataOffset,
           nvram_data.size() - FirmwareManagementParameters::kCrcDataOffset);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));
  EXPECT_TRUE(fwmp_.Load());
}

// Load fails on different major version
TEST_F(FirmwareManagementParametersTest, LoadMajorVersion) {
  SecureBlob nvram_data(kContentsWithHash);

  // Alter major version
  nvram_data[2] += 0x10;

  // Recalculate CRC
  nvram_data[0] =
      crc8(nvram_data.data() + FirmwareManagementParameters::kCrcDataOffset,
           nvram_data.size() - FirmwareManagementParameters::kCrcDataOffset);

  EXPECT_CALL(tpm_, IsNvramDefined(FirmwareManagementParameters::kNvramIndex))
      .WillOnce(Return(true));
  EXPECT_CALL(tpm_, ReadNvram(FirmwareManagementParameters::kNvramIndex, _))
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<1>(nvram_data), Return(true)));
  EXPECT_FALSE(fwmp_.Load());
}

class FirmwareManagementParametersPlatformIndexTest
    : public FirmwareManagementParametersTestBase<
          FirmwareManagementParameters::ResetMethod::kStoreDefaultFlags,
          FirmwareManagementParameters::WriteProtectionMethod::
              kOwnerAuthorization> {};

// Store flags and hash
TEST_F(FirmwareManagementParametersPlatformIndexTest, StoreFlagsAndHash) {
  SecureBlob nvram_data;
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsWithHash);
}

// Store flags only
TEST_F(FirmwareManagementParametersPlatformIndexTest, StoreFlagsOnly) {
  SecureBlob nvram_data;
  fwmp_hash_ptr_ = nullptr;
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsNoHash);
}

TEST_F(FirmwareManagementParametersPlatformIndexTest, CreateSetsDefaultFlags) {
  SecureBlob default_nvram_data;
  SetExpectationForStore(&default_nvram_data);
  EXPECT_TRUE(fwmp_.Store(/*flags=*/0, /*developer_key_hash=*/nullptr));
  EXPECT_TRUE(fwmp_.IsLoaded());

  // Modify the content of FWMP.
  SecureBlob nvram_data;
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsWithHash);

  // `Create()` is supposed to write the default content.
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Create());
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data.to_string(), default_nvram_data.to_string());

  // Modify the content of FWMP again.
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Store(fwmp_flags_, fwmp_hash_ptr_));
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data, kContentsWithHash);

  // `Destroy()` is supposed to write the default content.
  SetExpectationForStore(&nvram_data);
  EXPECT_TRUE(fwmp_.Destroy());
  EXPECT_TRUE(fwmp_.IsLoaded());
  EXPECT_EQ(nvram_data.to_string(), default_nvram_data.to_string());
}

}  // namespace cryptohome

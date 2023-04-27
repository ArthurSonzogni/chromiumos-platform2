// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/mock_bus.h>
#include <featured/proto_bindings/featured.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bootlockbox-client/bootlockbox/boot_lockbox_client.h"
// Note that boot_lockbox_rpc.pb.h have to be included before
// bootlockbox-client/bootlockbox/dbus-proxies.h because it is used in
// there.
#include "bootlockbox/proto_bindings/boot_lockbox_rpc.pb.h"
#include "featured/hmac.h"
#include "featured/store_impl.h"

#include "bootlockbox-client/bootlockbox/dbus-proxies.h"

namespace featured {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

constexpr char kEmptyStore[] = "";
constexpr char kInitialBootKey[] = "test_secret_key";
constexpr char kCorruptFileContent[] = "test_corrupted_data";

class MockBootLockboxClient : public bootlockbox::BootLockboxClient {
 public:
  explicit MockBootLockboxClient(scoped_refptr<dbus::MockBus> mock_bus)
      : BootLockboxClient(
            std::make_unique<org::chromium::BootLockboxInterfaceProxy>(
                mock_bus),
            mock_bus) {}
  ~MockBootLockboxClient() override = default;

  MOCK_METHOD(bool,
              Store,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool, Read, (const std::string&, std::string*), (override));
};

class StoreImplTest : public testing::Test {
 public:
  StoreImplTest()
      : mock_bus_(base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{})) {
    EXPECT_TRUE(dir_.CreateUniqueTempDir());
    boot_lockbox_client_ =
        std::make_unique<StrictMock<MockBootLockboxClient>>(mock_bus_);
    store_path_ = get_dir().Append("store");
    hmac_path_ = get_dir().Append("store_hmac");
    tpm_seed_path_ = get_dir().Append("tpm_seed");
  }

  const base::FilePath& get_dir() { return dir_.GetPath(); }

 protected:
  void InitializeStore(Store& store) {
    store.set_boot_attempts_since_last_seed_update(0);
    SeedDetails* seed = store.mutable_last_good_seed();
    seed->set_compressed_data("test_compressed_data");
    seed->set_date(1);
    seed->set_fetch_time(1);
    seed->set_locale("test_locale");
    seed->set_milestone(110);
    seed->set_permanent_consistency_country("us");
    seed->set_session_consistency_country("us");
    seed->set_signature("test_signature");
  }

  std::unique_ptr<MockBootLockboxClient> boot_lockbox_client_;
  base::FilePath store_path_;
  base::FilePath hmac_path_;
  base::FilePath tpm_seed_path_;
  std::string store_content_;
  std::string hmac_content_;

 private:
  base::ScopedTempDir dir_;
  scoped_refptr<dbus::MockBus> mock_bus_;
};

// Check that StoreImpl object is created when reading the key from the boot
// lockbox fails. Verifies an empty store and associated HMAC are written to
// disk.
//
// NOTE: This also tests the case when the store and hmac don't already exist
// (eg. when using StoreImpl for the first time).
TEST_F(StoreImplTest, LockboxReadKey_Failure) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_NE(store_interface, nullptr);

  // Verify that the on-disk contents are valid.
  HMAC hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(hmac_wrapper.Init(symmetric_key));

  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));

  EXPECT_EQ(store_content_, kEmptyStore);
  EXPECT_TRUE(hmac_wrapper.Verify(store_content_, hmac_content_));
}

// Check that StoreImpl creation fails when storing the key to the boot lockbox
// fails.
TEST_F(StoreImplTest, LockboxStoreKey_Failure) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _)).WillOnce(Return(false));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when the boot lockbox is invalid (eg.
// nullptr).
TEST_F(StoreImplTest, LockboxNull_Failure) {
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, /*boot_lockbox_client=*/nullptr);
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when reading the store file fails.
TEST_F(StoreImplTest, StoreRead_Failure) {
  // Create store with only write permission.
  base::File store_file(
      store_path_, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(store_file.IsValid());
  ASSERT_TRUE(SetPosixFilePermissions(store_path_,
                                      base::FILE_PERMISSION_WRITE_BY_USER));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when reading the hmac file fails.
TEST_F(StoreImplTest, HMACRead_Failure) {
  // Create store with only write permission.
  base::File hmac_file(hmac_path_,
                       base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(hmac_file.IsValid());
  ASSERT_TRUE(
      SetPosixFilePermissions(hmac_path_, base::FILE_PERMISSION_WRITE_BY_USER));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when store file creation fails due to
// incorrect permissions.
TEST_F(StoreImplTest, StoreCreate_Failure_WrongPermissions) {
  // Modify directory to have only read permission.
  ASSERT_TRUE(
      SetPosixFilePermissions(get_dir(), base::FILE_PERMISSION_READ_BY_USER));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);

  // Add execute and write permissions back to more likely to clean up the
  // directory correctly.
  ASSERT_TRUE(SetPosixFilePermissions(get_dir(),
                                      base::FILE_PERMISSION_EXECUTE_BY_USER |
                                          base::FILE_PERMISSION_WRITE_BY_USER));
}

// Check that StoreImpl creation fails when store file path is invalid (eg.
// empty path).
TEST_F(StoreImplTest, StoreCreate_Failure_InvalidPath) {
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      /*store_path=*/base::FilePath(""), hmac_path_, tpm_seed_path_,
      std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when writing to the store file fails.
TEST_F(StoreImplTest, StoreWrite_Failure) {
  // Create store with only read permission.
  base::File store_file(
      store_path_, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(store_file.IsValid());
  ASSERT_TRUE(
      SetPosixFilePermissions(store_path_, base::FILE_PERMISSION_READ_BY_USER));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl creation fails when writing the hmac file fails.
TEST_F(StoreImplTest, HMACWrite_Failure) {
  // Create store hmac with only read permission.
  base::File hmac_file(hmac_path_,
                       base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(hmac_file.IsValid());
  ASSERT_TRUE(
      SetPosixFilePermissions(hmac_path_, base::FILE_PERMISSION_READ_BY_USER));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_EQ(store_interface, nullptr);
}

// Check that StoreImpl object is created when the store is successfully
// verified. This means reading the boot lockbox key, the store file, and the
// hmac file from disk succeed as well.
//
// Verifies the store on disk is not modified and that a new store HMAC is
// written to disk.
TEST_F(StoreImplTest, StoreVerified_Success) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _))
      .WillOnce(Invoke([](const std::string& key, std::string* output) {
        *output = kInitialBootKey;
        return true;
      }));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  // Create store.
  Store store;
  InitializeStore(store);

  // Serialize store.
  std::string serialized_store;
  ASSERT_TRUE(store.SerializeToString(&serialized_store));

  // Write serialized store to disk.
  ASSERT_TRUE(base::WriteFile(store_path_, serialized_store));

  // Compute store HMAC.
  HMAC initial_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(initial_hmac_wrapper.Init(kInitialBootKey));

  std::optional<std::string> store_hmac =
      initial_hmac_wrapper.Sign(serialized_store);
  ASSERT_TRUE(store_hmac.has_value());

  // Write store HMAC to disk.
  std::string store_hmac_str = store_hmac.value();
  ASSERT_TRUE(base::WriteFile(hmac_path_, store_hmac_str));

  // Create StoreImpl object.
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_NE(store_interface, nullptr);

  // Verify that the on-disk contents are valid using the newly generated key.
  HMAC new_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(new_hmac_wrapper.Init(symmetric_key));

  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));

  EXPECT_EQ(store_content_, serialized_store);
  EXPECT_TRUE(new_hmac_wrapper.Verify(store_content_, hmac_content_));
}

// Check that StoreImpl object is created when store verification fails due to
// corrupt store file. Verifies an empty store and associated HMAC are written
// to disk.
TEST_F(StoreImplTest, StoreCorruption_Verification_Failure) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _))
      .WillOnce(Invoke([](const std::string& key, std::string* output) {
        *output = kInitialBootKey;
        return true;
      }));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  // Create store.
  Store store;
  InitializeStore(store);

  // Serialize store.
  std::string serialized_store;
  ASSERT_TRUE(store.SerializeToString(&serialized_store));

  // Corrupt store.
  store.set_boot_attempts_since_last_seed_update(-1);
  std::string corrupted_store;
  ASSERT_TRUE(store.SerializeToString(&corrupted_store));

  // Write corrupted store to disk.
  ASSERT_TRUE(base::WriteFile(store_path_, corrupted_store));

  // Compute HMAC for non-corrupted store.
  HMAC initial_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(initial_hmac_wrapper.Init(kInitialBootKey));

  std::optional<std::string> store_hmac =
      initial_hmac_wrapper.Sign(serialized_store);
  ASSERT_TRUE(store_hmac.has_value());

  // Write store HMAC to disk.
  std::string store_hmac_str = store_hmac.value();
  ASSERT_TRUE(base::WriteFile(hmac_path_, store_hmac_str));

  // Create StoreImpl object.
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_NE(store_interface, nullptr);

  // Verify that the on-disk contents are valid using the newly generated key.
  HMAC new_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(new_hmac_wrapper.Init(symmetric_key));

  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));

  EXPECT_EQ(store_content_, kEmptyStore);
  EXPECT_TRUE(new_hmac_wrapper.Verify(store_content_, hmac_content_));
}

// Check that StoreImpl object is created when store verification fails due to
// corrupt HMAC file. Verifies an empty store and associated HMAC are written to
// disk.
TEST_F(StoreImplTest, HMACCorruption_Verification_Failure) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _))
      .WillOnce(Invoke([](const std::string& key, std::string* output) {
        *output = kInitialBootKey;
        return true;
      }));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  // Create store.
  Store store;
  InitializeStore(store);

  // Serialize store.
  std::string serialized_store;
  ASSERT_TRUE(store.SerializeToString(&serialized_store));

  // Write serialized store to disk.
  ASSERT_TRUE(base::WriteFile(store_path_, serialized_store));

  // Write corrupted store HMAC to disk.
  ASSERT_TRUE(base::WriteFile(store_path_, kCorruptFileContent));

  // Create StoreImpl object.
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_NE(store_interface, nullptr);

  // Verify that the on-disk contents are valid using the newly generated key.
  HMAC new_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(new_hmac_wrapper.Init(symmetric_key));

  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));

  EXPECT_EQ(store_content_, kEmptyStore);
  EXPECT_TRUE(new_hmac_wrapper.Verify(store_content_, hmac_content_));
}

// Check that StoreImpl object is created when store deserialization fails due
// to corrupt store and corrupt hmac files. Note that both the store and hmac
// must be corrupted in a way where verification succeeds for the
// deserialization logic to be tested.
//
// Verifies an empty store and associated HMAC are written to disk.
TEST_F(StoreImplTest, StoreCorruption_Deserialize_Failure) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _))
      .WillOnce(Invoke([](const std::string& key, std::string* output) {
        *output = kInitialBootKey;
        return true;
      }));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  // Write corrupted store to disk.
  std::string corrupted_store = kCorruptFileContent;
  ASSERT_TRUE(base::WriteFile(store_path_, corrupted_store));

  // Compute HMAC for corrupted store.
  HMAC initial_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(initial_hmac_wrapper.Init(kInitialBootKey));

  std::optional<std::string> corrupted_hmac =
      initial_hmac_wrapper.Sign(corrupted_store);
  ASSERT_TRUE(corrupted_hmac.has_value());

  // Write store HMAC to disk.
  std::string corrupted_hmac_str = corrupted_hmac.value();
  ASSERT_TRUE(base::WriteFile(hmac_path_, corrupted_hmac_str));

  // Create StoreImpl object.
  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  EXPECT_NE(store_interface, nullptr);

  // Verify that the on-disk contents are valid using the newly generated key.
  HMAC new_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(new_hmac_wrapper.Init(symmetric_key));

  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));

  EXPECT_EQ(store_content_, kEmptyStore);
  EXPECT_TRUE(new_hmac_wrapper.Verify(store_content_, hmac_content_));
}

// Check correctness of incrementing the boot attempts field in the store.
TEST_F(StoreImplTest, IncrementBootAttempts_Success) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Check boot attempts field.
  EXPECT_EQ(store_interface->GetBootAttemptsSinceLastUpdate(), 0);

  // Increment boot attempts.
  EXPECT_TRUE(store_interface->IncrementBootAttemptsSinceLastUpdate());
  EXPECT_EQ(store_interface->GetBootAttemptsSinceLastUpdate(), 1);

  // Verify boot attempts update is reflected on disk.
  HMAC hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(hmac_wrapper.Init(symmetric_key));
  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));
  EXPECT_TRUE(hmac_wrapper.Verify(store_content_, hmac_content_));

  Store store;
  ASSERT_TRUE(store.ParseFromString(store_content_));
  EXPECT_EQ(store.boot_attempts_since_last_seed_update(), 1);
}

// Check that incrementing the boot attempts field fails when StoreImpl does not
// have permission to update the store file on disk.
TEST_F(StoreImplTest, IncrementBootAttempts_Failure_StoreWrongPermissions) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _)).WillOnce(Return(true));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Remove write permissions for store file.
  ASSERT_TRUE(
      SetPosixFilePermissions(store_path_, base::FILE_PERMISSION_READ_BY_USER));

  // Increment boot attempts.
  EXPECT_FALSE(store_interface->IncrementBootAttemptsSinceLastUpdate());
}

// Check that incrementing the boot attempts field fails when StoreImpl does not
// have permission to update the hmac file on disk.
TEST_F(StoreImplTest, IncrementBootAttempts_Failure_HMACWrongPermissions) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _)).WillOnce(Return(true));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Remove write permissions for hmac file.
  ASSERT_TRUE(
      SetPosixFilePermissions(hmac_path_, base::FILE_PERMISSION_READ_BY_USER));

  // Increment boot attempts.
  EXPECT_FALSE(store_interface->IncrementBootAttemptsSinceLastUpdate());
}

// Check correctness of clearing the boot attempts field in the store.
TEST_F(StoreImplTest, ClearBootAttempts_Success) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _))
      .WillOnce(Invoke([](const std::string& key, std::string* output) {
        *output = kInitialBootKey;
        return true;
      }));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  // Create store.
  Store store;
  store.set_boot_attempts_since_last_seed_update(1);

  // Serialize store.
  std::string serialized_store;
  ASSERT_TRUE(store.SerializeToString(&serialized_store));

  // Write serialized store to disk.
  ASSERT_TRUE(base::WriteFile(store_path_, serialized_store));

  // Compute store HMAC.
  HMAC initial_hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(initial_hmac_wrapper.Init(kInitialBootKey));

  std::optional<std::string> store_hmac =
      initial_hmac_wrapper.Sign(serialized_store);
  ASSERT_TRUE(store_hmac.has_value());

  // Write store HMAC to disk.
  std::string store_hmac_str = store_hmac.value();
  ASSERT_TRUE(base::WriteFile(hmac_path_, store_hmac_str));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Check boot attempts field.
  EXPECT_EQ(store_interface->GetBootAttemptsSinceLastUpdate(), 1);

  // Clear boot attempts.
  EXPECT_TRUE(store_interface->ClearBootAttemptsSinceLastUpdate());
  EXPECT_EQ(store_interface->GetBootAttemptsSinceLastUpdate(), 0);

  // Verify boot attempts update is reflected on disk.
  HMAC hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(hmac_wrapper.Init(symmetric_key));
  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));
  EXPECT_TRUE(hmac_wrapper.Verify(store_content_, hmac_content_));

  ASSERT_TRUE(store.ParseFromString(store_content_));
  EXPECT_EQ(store.boot_attempts_since_last_seed_update(), 0);
}

// Check that clearing the boot attempts field fails when StoreImpl does not
// have permission to update the store file on disk.
TEST_F(StoreImplTest, ClearBootAttempts_Failure_StoreWrongPermissions) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _)).WillOnce(Return(true));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Remove write permissions for store file.
  ASSERT_TRUE(
      SetPosixFilePermissions(store_path_, base::FILE_PERMISSION_READ_BY_USER));

  // Clear boot attempts.
  EXPECT_FALSE(store_interface->ClearBootAttemptsSinceLastUpdate());
}

// Check correctness of updating the seed field in the store.
TEST_F(StoreImplTest, UpdateSeed_Success) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _))
      .WillOnce(Invoke(
          [&symmetric_key](const std::string& key, const std::string& input) {
            symmetric_key = input;
            return true;
          }));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Check seed field.
  SeedDetails seed;
  EXPECT_THAT(store_interface->GetLastGoodSeed(),
              EqualsProto(seed));  // Check for empty seed.

  // Update seed.
  seed.set_compressed_data("test_compressed_data");
  EXPECT_TRUE(store_interface->SetLastGoodSeed(seed));
  EXPECT_THAT(store_interface->GetLastGoodSeed(), EqualsProto(seed));

  // Verify seed update is reflected on disk.
  Store store;
  HMAC hmac_wrapper(HMAC::SHA256);
  ASSERT_TRUE(hmac_wrapper.Init(symmetric_key));
  ASSERT_TRUE(ReadFileToString(store_path_, &store_content_));
  ASSERT_TRUE(ReadFileToString(hmac_path_, &hmac_content_));
  EXPECT_TRUE(hmac_wrapper.Verify(store_content_, hmac_content_));

  ASSERT_TRUE(store.ParseFromString(store_content_));
  EXPECT_THAT(store.last_good_seed(), EqualsProto(seed));
}

// Check that updating the seed field fails when StoreImpl does not
// have permission to update the store file on disk.
TEST_F(StoreImplTest, UpdateSeed_Failure_StoreWrongPermissions) {
  EXPECT_CALL(*boot_lockbox_client_, Read(_, _)).WillOnce(Return(false));

  std::string symmetric_key;
  EXPECT_CALL(*boot_lockbox_client_, Store(_, _)).WillOnce(Return(true));

  std::unique_ptr<StoreInterface> store_interface = StoreImpl::Create(
      store_path_, hmac_path_, tpm_seed_path_, std::move(boot_lockbox_client_));
  ASSERT_NE(store_interface, nullptr);

  // Remove write permissions for store file.
  ASSERT_TRUE(
      SetPosixFilePermissions(store_path_, base::FILE_PERMISSION_READ_BY_USER));

  // Update seed.
  SeedDetails seed;
  seed.set_compressed_data("test_compressed_data");
  EXPECT_FALSE(store_interface->SetLastGoodSeed(seed));
}
}  // namespace featured

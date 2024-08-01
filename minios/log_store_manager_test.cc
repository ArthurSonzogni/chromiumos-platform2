// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/log_store_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/file_utils.h>
#include <brillo/secure_blob.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>
#include <libstorage/platform/mock_platform.h>
#include <minios/proto_bindings/minios.pb.h>
#include <vpd/fake_vpd.h>
#include <vpd/vpd.h>

#include "minios/mock_disk_util.h"
#include "minios/mock_log_store_manifest.h"
#include "minios/mock_process_manager.h"
#include "minios/utils.h"

using LogDirection = minios::LogStoreManager::LogDirection;
using testing::_;
using testing::Contains;
using testing::DoAll;
using testing::Invoke;
using testing::IsSupersetOf;
using testing::NiceMock;
using testing::Optional;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

namespace minios {

const brillo::SecureBlob kTestData{
    "test data to verify encryption and decryption"};
const brillo::SecureBlob kValidKey{"thisisa32bytestring1234567890abc"};

TEST(LogStoreManagerEncryptTest, EncryptLogsTest) {
  base::ScopedTempDir tmp_dir_;
  ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());

  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  const auto archive_path = tmp_dir_.GetPath().Append("archive");
  ASSERT_TRUE(base::WriteFile(archive_path, kTestData));
  auto encrypted_archive = log_store_manager_->EncryptLogs(archive_path);
  EXPECT_TRUE(encrypted_archive.has_value());

  EXPECT_TRUE(log_store_manager_->encrypt_key_.has_value());
  auto archive = DecryptLogArchive(encrypted_archive.value(),
                                   log_store_manager_->encrypt_key_.value());
  EXPECT_EQ(archive.value(), kTestData);
}

TEST(LogStoreManagerInitTest, InitTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  auto mock_disk_util = std::make_unique<MockDiskUtil>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto mock_process_manager_ = std::make_shared<NiceMock<MockProcessManager>>();
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();
  // Kernel size = 20000.
  const std::string futility_output =
      std::string{"kernel::keyblock::size::10\n"} +
      std::string{"kernel::preamble::size::10\n"} +
      std::string{"kernel::body::size::19980\n"};

  stub_crossystem->VbSetSystemPropertyString("minios_priority", "A");
  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{"/dev/nvme0n1"}));
  EXPECT_CALL(*platform, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(80 * 512), Return(true)));
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      Return(true)));

  log_store_manager_->process_manager_ = mock_process_manager_;

  EXPECT_TRUE(log_store_manager_->Init(std::move(mock_disk_util),
                                       std::move(stub_crossystem),
                                       std::move(platform)));
}

TEST(LogStoreManagerInitTest, InitSpecifiedPartitionTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>(9);
  auto mock_disk_util = std::make_unique<MockDiskUtil>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto mock_process_manager = std::make_shared<NiceMock<MockProcessManager>>();
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();

  // Kernel size = 20000.
  const std::string futility_output =
      std::string{"kernel::keyblock::size::10\n"} +
      std::string{"kernel::preamble::size::10\n"} +
      std::string{"kernel::body::size::19980\n"};

  // Set crossystem to not know minios_priority, init should succeed since we
  // specified a partition.
  stub_crossystem->VbSetSystemPropertyString("minios_priority", "undefined");
  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{"/dev/nvme0n1"}));
  EXPECT_CALL(*platform, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(80 * 512), Return(true)));
  EXPECT_CALL(*mock_process_manager, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      Return(true)));

  log_store_manager_->process_manager_ = mock_process_manager;

  EXPECT_TRUE(log_store_manager_->Init(std::move(mock_disk_util),
                                       std::move(stub_crossystem),
                                       std::move(platform)));
}

TEST(LogStoreManagerInitTest, InitNoFixedDriveTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  auto mock_disk_util = std::make_unique<StrictMock<MockDiskUtil>>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();

  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{""}));

  EXPECT_FALSE(log_store_manager_->Init(std::move(mock_disk_util),
                                        std::move(stub_crossystem),
                                        std::move(platform)));
}

TEST(LogStoreManagerInitTest, InitUnknownPartitionSizeTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  auto mock_disk_util = std::make_unique<MockDiskUtil>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();

  stub_crossystem->VbSetSystemPropertyString("minios_priority", "A");
  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{"/dev/nvme0n1"}));
  EXPECT_CALL(*platform, GetBlkSize(_, _)).WillOnce(Return(false));

  EXPECT_FALSE(log_store_manager_->Init(std::move(mock_disk_util),
                                        std::move(stub_crossystem),
                                        std::move(platform)));
}

TEST(LogStoreManagerInitTest, InitUnknownPartitionTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  auto mock_disk_util = std::make_unique<StrictMock<MockDiskUtil>>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();

  stub_crossystem->VbSetSystemPropertyString("minios_priority", "");
  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{"/dev/nvme0n1"}));

  EXPECT_FALSE(log_store_manager_->Init(std::move(mock_disk_util),
                                        std::move(stub_crossystem),
                                        std::move(platform)));
}

TEST(LogStoreManagerInitTest, InitKernelLogStoreOverlapTest) {
  auto log_store_manager_ = std::make_shared<LogStoreManager>();
  auto mock_disk_util = std::make_unique<MockDiskUtil>();
  auto stub_crossystem = std::make_unique<crossystem::Crossystem>(
      std::make_unique<crossystem::fake::CrossystemFake>());
  auto platform = std::make_unique<StrictMock<libstorage::MockPlatform>>();
  auto mock_process_manager_ = std::make_shared<NiceMock<MockProcessManager>>();

  // Kernel size = 20000.
  const std::string futility_output =
      std::string{"kernel::keyblock::size::10\n"} +
      std::string{"kernel::preamble::size::10\n"} +
      std::string{"kernel::body::size::19980\n"};

  stub_crossystem->VbSetSystemPropertyString("minios_priority", "A");
  EXPECT_CALL(*mock_disk_util, GetFixedDrive())
      .WillOnce(Return(base::FilePath{"/dev/nvme0n1"}));
  EXPECT_CALL(*platform, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(60 * 512), Return(true)));
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      Return(true)));

  log_store_manager_->process_manager_ = mock_process_manager_;

  EXPECT_FALSE(log_store_manager_->Init(std::move(mock_disk_util),
                                        std::move(stub_crossystem),
                                        std::move(platform)));
}

class LogStoreManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    const auto fixed_drive = tmp_dir_.GetPath().Append("disk");
    disk_path_ = brillo::AppendPartition(fixed_drive, 9);
    base::File disk_file_{
        disk_path_, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE};

    EXPECT_TRUE(disk_file_.IsValid());
    disk_file_.SetLength(partition_size_);
    disk_file_.Close();

    auto vpd = std::make_shared<vpd::Vpd>(std::make_unique<vpd::FakeVpd>());

    EXPECT_CALL(*mock_disk_util_, GetFixedDrive())
        .WillOnce(Return(fixed_drive));
    stub_crossystem_->VbSetSystemPropertyString("minios_priority", "A");
    EXPECT_CALL(*mock_storage_platform_, GetBlkSize(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(partition_size_), Return(true)));

    const std::string futility_output =
        std::string{"kernel_partition::/dev/nvme0n1p9\n"} +
        std::string{"kernel::keyblock::size::100\n"} +
        std::string{"kernel::preamble::size::100\n"} +
        std::string{"kernel::preamble::body::load_address::0x100000\n"} +
        std::string{"kernel::body::size::25400\n"};
    EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                        testing::Return(true)));

    log_store_manager_ =
        std::make_shared<LogStoreManager>(9, mock_process_manager_, vpd);
    EXPECT_TRUE(log_store_manager_->Init(std::move(mock_disk_util_),
                                         std::move(stub_crossystem_),
                                         std::move(mock_storage_platform_)));

    auto log_store_manifest =
        std::make_unique<StrictMock<MockLogStoreManifest>>();
    log_store_manifest_ptr_ = log_store_manifest.get();
    log_store_manager_->SetLogStoreManifest(std::move(log_store_manifest));
  }

  std::shared_ptr<MockProcessManager> mock_process_manager_ =
      std::make_shared<NiceMock<MockProcessManager>>();
  MockLogStoreManifest* log_store_manifest_ptr_;
  std::unique_ptr<MockDiskUtil> mock_disk_util_ =
      std::make_unique<MockDiskUtil>();
  std::unique_ptr<crossystem::Crossystem> stub_crossystem_ =
      std::make_unique<crossystem::Crossystem>(
          std::make_unique<crossystem::fake::CrossystemFake>());
  std::unique_ptr<libstorage::MockPlatform> mock_storage_platform_ =
      std::make_unique<StrictMock<libstorage::MockPlatform>>();

  std::shared_ptr<LogStoreManager> log_store_manager_;

  base::ScopedTempDir tmp_dir_;
  base::FilePath disk_path_;
  uint64_t kernel_size_ = 25600;
  uint64_t partition_size_ = 51200;
};

void WriteToArchive(
    const std::shared_ptr<MockProcessManager> mock_process_manager) {
  // Inject data into the file we are archiving to.
  EXPECT_CALL(*mock_process_manager, RunCommand(Contains("/bin/tar"), _))
      .WillOnce(DoAll(
          WithArg<0>(Invoke([](const std::vector<std::string>& compress_args) {
            ASSERT_GT(compress_args.size(), 3);
            const auto archive_path = base::FilePath{compress_args[2]};
            ASSERT_TRUE(base::WriteFile(archive_path, kTestData));
          })),
          Return(0)));
}

TEST_F(LogStoreManagerTest, SaveLogsToDiskTest) {
  EXPECT_CALL(*log_store_manifest_ptr_, Generate(_));
  EXPECT_CALL(*log_store_manifest_ptr_, Write()).WillOnce(Return(true));

  WriteToArchive(mock_process_manager_);

  EXPECT_TRUE(log_store_manager_->SaveLogs(
      LogStoreManagerInterface::LogDirection::Disk));

  // Verify encrypted archive is written to destination.
  base::ScopedFD fd(brillo::OpenSafely(disk_path_, O_RDONLY, 0));
  ASSERT_EQ(lseek(fd.get(), partition_size_ - kLogStoreOffset, SEEK_SET),
            partition_size_ - kLogStoreOffset);
  EncryptedLogFile encrypted_archive;
  encrypted_archive.ParseFromFileDescriptor(fd.get());
  auto archive = DecryptLogArchive(encrypted_archive,
                                   log_store_manager_->encrypt_key_.value());
  EXPECT_EQ(archive.value(), kTestData);
}

TEST_F(LogStoreManagerTest, SaveLogsToPathTest) {
  const auto& dest_path = tmp_dir_.GetPath().Append("encrypted_logs.tar");

  WriteToArchive(mock_process_manager_);

  EXPECT_TRUE(log_store_manager_->SaveLogs(
      LogStoreManagerInterface::LogDirection::Stateful, dest_path));

  // Verify encrypted archive are written to destination.
  EncryptedLogFile encrypted_archive;
  base::ScopedFD fd(brillo::OpenSafely(dest_path, O_RDONLY, 0));
  encrypted_archive.ParseFromFileDescriptor(fd.get());
  auto archive = DecryptLogArchive(encrypted_archive,
                                   log_store_manager_->encrypt_key_.value());
  EXPECT_EQ(archive.value(), kTestData);
}

TEST_F(LogStoreManagerTest, SaveLogsToRemovable) {
  auto removable_path = tmp_dir_.GetPath().Append("removable");
  WriteToArchive(mock_process_manager_);

  EXPECT_TRUE(log_store_manager_->SaveLogs(LogDirection::RemovableDevice,
                                           removable_path));
  const auto removable_archive = ReadFileToSecureBlob(removable_path);
  EXPECT_TRUE(removable_archive.has_value());
  EXPECT_EQ(removable_archive.value(), kTestData);
}

TEST_F(LogStoreManagerTest, SaveLogsNoPathTest) {
  EXPECT_FALSE(
      log_store_manager_->SaveLogs(LogDirection::Stateful, std::nullopt));
  EXPECT_FALSE(log_store_manager_->SaveLogs(LogDirection::RemovableDevice,
                                            std::nullopt));
}

TEST_F(LogStoreManagerTest, ClearLogsTest) {
  LogManifest manifest;
  manifest.mutable_entry()->set_offset(48000);
  manifest.mutable_entry()->set_count(1025);
  const auto clear_size = partition_size_ - 48000;

  EXPECT_CALL(*log_store_manifest_ptr_, Retrieve()).WillOnce(Return(manifest));
  EXPECT_CALL(*log_store_manifest_ptr_, Clear());

  EXPECT_TRUE(log_store_manager_->ClearLogs());
  std::string disk_archive;
  disk_archive.resize(clear_size);

  base::File disk_file{disk_path_,
                       base::File::FLAG_OPEN | base::File::FLAG_READ};
  disk_file.ReadAndCheck(
      48000, {reinterpret_cast<uint8_t*>(disk_archive.data()), clear_size});
}

TEST_F(LogStoreManagerTest, ClearLogsInKernelTest) {
  LogManifest manifest;
  manifest.mutable_entry()->set_offset(kernel_size_);
  manifest.mutable_entry()->set_count(1025);

  EXPECT_CALL(*log_store_manifest_ptr_, Retrieve()).WillOnce(Return(manifest));

  EXPECT_FALSE(log_store_manager_->ClearLogs());
}

TEST_F(LogStoreManagerTest, FetchRemovableDeviceLogs) {
  EXPECT_EQ(log_store_manager_->FetchLogs(LogDirection::RemovableDevice,
                                          base::FilePath{""}, kValidKey),
            std::nullopt);
}

TEST_F(LogStoreManagerTest, FetchDiskLogsTest) {
  // Write encrypted data to disk, and expect it to be properly decrypted and
  // written to `unencrypted` file.
  const uint64_t offset = kernel_size_ + 1;

  const auto encrypted_data = EncryptLogArchive(kTestData, kValidKey);
  base::File disk_file{disk_path_,
                       base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE};
  ASSERT_TRUE(disk_file.WriteAndCheck(
      offset, {reinterpret_cast<const uint8_t*>(
                   encrypted_data->SerializeAsString().c_str()),
               encrypted_data->ByteSizeLong()}));
  disk_file.Close();
  // Create a manifest for above encrypted data.
  LogManifest manifest;
  manifest.mutable_entry()->set_offset(offset);
  manifest.mutable_entry()->set_count(encrypted_data->ByteSizeLong());

  EXPECT_CALL(*mock_process_manager_,
              RunCommand(IsSupersetOf({"/bin/tar", "-xzf"}), _))
      .WillOnce(Return(0));
  EXPECT_CALL(*log_store_manifest_ptr_, Retrieve()).WillOnce(Return(manifest));

  EXPECT_THAT(
      log_store_manager_->FetchLogs(LogDirection::Disk, tmp_dir_.GetPath(),
                                    kValidKey, std::nullopt),
      Optional(true));
}

TEST_F(LogStoreManagerTest, FetchDiskLogsInKernel) {
  // Create a manifest for above encrypted data.
  LogManifest manifest;
  manifest.mutable_entry()->set_offset(kernel_size_);
  manifest.mutable_entry()->set_count(10);
  EXPECT_CALL(*log_store_manifest_ptr_, Retrieve()).WillOnce(Return(manifest));

  EXPECT_EQ(log_store_manager_->FetchLogs(LogDirection::Disk,
                                          tmp_dir_.GetPath(), kValidKey),
            std::nullopt);
}

TEST_F(LogStoreManagerTest, FetchDiskLogsNoManifestTest) {
  EXPECT_CALL(*log_store_manifest_ptr_, Retrieve())
      .WillOnce(Return(std::nullopt));

  EXPECT_THAT(log_store_manager_->FetchLogs(LogDirection::Disk,
                                            tmp_dir_.GetPath(), kValidKey),
              Optional(false));
}

TEST_F(LogStoreManagerTest, FetchStatefulLogs) {
  const auto archive_path = tmp_dir_.GetPath().Append("archive");
  const auto encrypted_data = EncryptLogArchive(kTestData, kValidKey);
  ASSERT_TRUE(
      base::WriteFile(archive_path, encrypted_data->SerializeAsString()));

  EXPECT_CALL(*mock_process_manager_,
              RunCommand(IsSupersetOf({"/bin/tar", "-xzf"}), _))
      .WillOnce(Return(0));

  EXPECT_THAT(
      log_store_manager_->FetchLogs(LogDirection::Stateful, tmp_dir_.GetPath(),
                                    kValidKey, archive_path),
      Optional(true));
}

TEST_F(LogStoreManagerTest, FetchStatefulNoPathLogs) {
  EXPECT_EQ(log_store_manager_->FetchLogs(LogDirection::Stateful,
                                          tmp_dir_.GetPath(), kValidKey),
            std::nullopt);
}

}  // namespace minios

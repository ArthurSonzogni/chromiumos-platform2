// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_device_metrics/flex_device_metrics.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "flex_hwis/flex_device_metrics/flex_device_metrics_fwupd.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrictMock;

namespace {

void CreatePartitionDir(const base::FilePath& dir,
                        const std::string& partition_label,
                        int size_in_blocks) {
  CHECK(base::CreateDirectory(dir));
  CHECK(base::WriteFile(dir.Append("uevent"), "PARTNAME=" + partition_label));
  CHECK(base::WriteFile(dir.Append("size"), std::to_string(size_in_blocks)));
}

void ExpectSuccessfulKernAMetric(MetricsLibraryMock& metrics) {
  EXPECT_CALL(metrics, SendSparseToUMA("Platform.FlexPartitionSize.KERN-A", 16))
      .WillOnce(Return(true));
}

// Create a vector of `FwupdDeviceHistory`. This serves as the expected
// value for various tests.
std::vector<FwupdDeviceHistory> CreateExpectedDeviceHistory() {
  const FwupdDeviceHistory device = {"abc",
                                     std::string(kUefiCapsulePlugin),
                                     base::Time::UnixEpoch() + base::Seconds(1),
                                     FwupdUpdateState::kSuccess,
                                     {{FwupdLastAttemptStatus::kSuccess}}};
  std::vector<FwupdDeviceHistory> devices;
  devices.push_back(device);
  return devices;
}

// Create a vector of fwupd device histories, simulating what would be
// produced by calling the GetHistory dbus method. It contains
// data equivalent to `CreateDeviceHistory`.
std::vector<brillo::VariantDictionary> CreateValidRawDevices() {
  std::map<std::string, std::string> raw_metadata;
  raw_metadata["LastAttemptStatus"] = std::string("0x0");

  std::vector<brillo::VariantDictionary> raw_releases;
  brillo::VariantDictionary raw_release;
  raw_release["Metadata"] = raw_metadata;
  raw_releases.push_back(raw_release);

  brillo::VariantDictionary raw_device;
  raw_device["Name"] = std::string("abc");
  raw_device["Plugin"] = std::string(kUefiCapsulePlugin);
  raw_device["Modified"] = static_cast<uint64_t>(1);
  raw_device["UpdateState"] = static_cast<uint32_t>(FwupdUpdateState::kSuccess);
  raw_device["Release"] = raw_releases;

  std::vector<brillo::VariantDictionary> raw_devices;
  raw_devices.push_back(raw_device);

  return raw_devices;
}

// Create a `dbus::Response` with data equivalent to `CreateDeviceHistory`.
std::unique_ptr<dbus::Response> CreateValidGetHistoryResponse() {
  auto resp = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(resp.get());

  brillo::dbus_utils::AppendValueToWriter(&writer, CreateValidRawDevices());

  return resp;
}

}  // namespace

// Test blocks-to-MiB conversion.
TEST(FlexDiskMetrics, ConvertBlocksToMiB) {
  EXPECT_EQ(ConvertBlocksToMiB(0), 0);
  EXPECT_EQ(ConvertBlocksToMiB(2048), 1);
  EXPECT_EQ(ConvertBlocksToMiB(4096), 2);

  // Round down.
  EXPECT_EQ(ConvertBlocksToMiB(4095), 1);
}

TEST(FlexDiskMetrics, GetPartitionLabelFromUevent) {
  base::ScopedTempDir partition_dir;
  CHECK(partition_dir.CreateUniqueTempDir());

  // Error: uevent file does not exist.
  EXPECT_FALSE(
      GetPartitionLabelFromUevent(partition_dir.GetPath()).has_value());

  // Error: uevent file does not contain PARTNAME.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("uevent"), "MAJOR=8\n"));
  EXPECT_FALSE(
      GetPartitionLabelFromUevent(partition_dir.GetPath()).has_value());

  // Successfully get partition name.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("uevent"),
                        "MAJOR=8\nPARTNAME=EFI-SYSTEM"));
  EXPECT_EQ(GetPartitionLabelFromUevent(partition_dir.GetPath()), "EFI-SYSTEM");
}

TEST(FlexDiskMetrics, GetPartitionSizeInMiB) {
  base::ScopedTempDir partition_dir;
  CHECK(partition_dir.CreateUniqueTempDir());

  // Error: size file does not exist.
  EXPECT_FALSE(GetPartitionSizeInMiB(partition_dir.GetPath()).has_value());

  // Error: size file is invalid.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("size"), "abc\n"));
  EXPECT_FALSE(GetPartitionSizeInMiB(partition_dir.GetPath()).has_value());

  // Successfully get partition size.
  CHECK(base::WriteFile(partition_dir.GetPath().Append("size"), "4096\n"));
  EXPECT_EQ(GetPartitionSizeInMiB(partition_dir.GetPath()), 2);
}

TEST(FlexDiskMetrics, GetPartitionSizeMap) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());
  const auto sys_block_root_path = root_dir.GetPath().Append("sys/block");
  CHECK(base::CreateDirectory(sys_block_root_path));

  // No results: sda directory does not exist.
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda directory is empty.
  const auto sda_path = sys_block_root_path.Append("sda");
  CHECK(base::CreateDirectory(sda_path));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: a directory containing valid partition data exists, but
  // it doesn't start with the device name so it's excluded.
  const auto power_dir = sda_path.Append("power");
  CreatePartitionDir(power_dir, "POWER", 4096);
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda1 directory doesn't provide a partition label.
  const auto sda1_dir = sda_path.Append("sda1");
  CreatePartitionDir(sda1_dir, "SDA1", 4096);
  brillo::DeleteFile(sda1_dir.Append("uevent"));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // No results: sda2 directory doesn't provide a partition size.
  const auto sda2_dir = sda_path.Append("sda2");
  CreatePartitionDir(sda2_dir, "SDA2", 4096);
  brillo::DeleteFile(sda2_dir.Append("size"));
  EXPECT_TRUE(GetPartitionSizeMap(root_dir.GetPath(), "sda").empty());

  // Create a normal sda3 partition.
  CreatePartitionDir(sda_path.Append("sda3"), "SDA3", 4096);
  // Create a sda4 and sda5 as "reserved" partitions that both have the
  // same label.
  CreatePartitionDir(sda_path.Append("sda4"), "reserved", 2048);
  CreatePartitionDir(sda_path.Append("sda5"), "reserved", 4096);

  // Check that the map contains the sda3/4/5 partitions.
  const auto label_to_size_map = GetPartitionSizeMap(root_dir.GetPath(), "sda");
  EXPECT_EQ(label_to_size_map.size(), 3);
  EXPECT_EQ(label_to_size_map.find("SDA3")->second, 2);
  EXPECT_EQ(label_to_size_map.count("reserved"), 2);
}

// Test successfully sending one metric.
TEST(FlexDiskMetrics, Success) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  EXPECT_TRUE(SendDiskMetrics(metrics, label_to_size_map, {"KERN-A"}));
}

// Test failure due to an expected partition not being present. Also
// verify that error doesn't prevent another metric from being sent.
TEST(FlexDiskMetrics, MissingPartitionFailure) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  // Since some metrics failed to send, expect failure.
  EXPECT_FALSE(
      SendDiskMetrics(metrics, label_to_size_map, {"missing", "KERN-A"}));
}

// Test failure due to multiple partitions having the same label. Also
// verify that error doesn't prevent another metric from being sent.
TEST(FlexDiskMetrics, MultiplePartitionFailure) {
  StrictMock<MetricsLibraryMock> metrics;
  ExpectSuccessfulKernAMetric(metrics);

  MapPartitionLabelToMiBSize label_to_size_map;
  label_to_size_map.insert(std::make_pair("KERN-A", 16));

  label_to_size_map.insert(std::make_pair("multiple", 32));
  label_to_size_map.insert(std::make_pair("multiple", 64));

  // Since some metrics failed to send, expect failure.
  EXPECT_FALSE(
      SendDiskMetrics(metrics, label_to_size_map, {"multiple", "KERN-A"}));
}

// Test successfully sending the CPU ISA level metric.
TEST(FlexCpuMetrics, SendCpuIsaLevelMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexCpuIsaLevel", 2, 5))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendCpuIsaLevelMetric(metrics, CpuIsaLevel::kX86_64_V2));
}

// Test getting the boot method in various circumstances.
TEST(FlexBootMethodMetrics, GetBootMethod) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());

  // Expect kBios if the VPD path and EFI path do not exist,
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kBios);

  // Expect kCoreboot if the VPD path exists.
  const auto vpd_sysfs_path = root_dir.GetPath().Append("sys/firmware/vpd/");
  CHECK(base::CreateDirectory(vpd_sysfs_path));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kCoreboot);

  // Expect kCoreboot if both the VPD path and EFI paths exist.
  const auto efi_sysfs_path = root_dir.GetPath().Append("sys/firmware/efi/");
  CHECK(base::CreateDirectory(efi_sysfs_path));

  // Delete the VPD path to move onto EFI.
  brillo::DeleteFile(vpd_sysfs_path);

  // Expect kUnknown if the EFI path exists but the value
  // of `fw_platform_size` is bad or missing.
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUnknown);
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "abcd"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUnknown);

  // Expect kUefi64 if the value of `fw_platform_size` is "64".
  brillo::DeleteFile(efi_sysfs_path.Append("fw_platform_size"));
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "64"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUefi64);

  // Expect kUefi32 if the value of `fw_platform_size` is "32".
  brillo::DeleteFile(efi_sysfs_path.Append("fw_platform_size"));
  CHECK(base::WriteFile(efi_sysfs_path.Append("fw_platform_size"), "32"));
  EXPECT_EQ(GetBootMethod(root_dir.GetPath()), BootMethod::kUefi32);
}

// Test successfully sending the boot method metric.
TEST(FlexBootMethodMetrics, SendBootMethodMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexBootMethod", 3, 5))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendBootMethodMetric(metrics, BootMethod::kUefi64));
}

// Test converting string to `InstallMethod`.
TEST(FlexInstallMethodMetrics, InstallMethodFromString) {
  ASSERT_EQ(InstallMethodFromString("flexor"), InstallMethod::kFlexor);
  ASSERT_EQ(InstallMethodFromString("mass-deploy"), InstallMethod::kMassDeploy);
  ASSERT_EQ(InstallMethodFromString("remote-deploy"),
            InstallMethod::kRemoteDeploy);

  ASSERT_EQ(InstallMethodFromString(""), InstallMethod::kUnknown);
  ASSERT_EQ(InstallMethodFromString("Flexor"), InstallMethod::kUnknown);
  ASSERT_EQ(InstallMethodFromString("flexors"), InstallMethod::kUnknown);
  ASSERT_EQ(InstallMethodFromString("aflexor"), InstallMethod::kUnknown);
  ASSERT_EQ(InstallMethodFromString(" flexor"), InstallMethod::kUnknown);
  ASSERT_EQ(InstallMethodFromString("flexor "), InstallMethod::kUnknown);
}

// Test reading `InstallState` based on the file.
TEST(FlexInstallMethodMetrics, GetInstallState) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());
  base::FilePath root_path = root_dir.GetPath();

  InstallState got = GetInstallState(root_path);
  EXPECT_EQ(got.just_installed, false);
  EXPECT_EQ(got.method, InstallMethod::kUnknown);

  const auto unencrypted_stateful_dir = root_dir.GetPath().Append(
      "mnt/stateful_partition/unencrypted/install_metrics");
  const auto install_type_path =
      unencrypted_stateful_dir.Append("install_type");
  ASSERT_TRUE(base::CreateDirectory(unencrypted_stateful_dir));

  ASSERT_TRUE(base::WriteFile(install_type_path, ""));
  got = GetInstallState(root_path);
  EXPECT_EQ(got.just_installed, true);
  EXPECT_EQ(got.method, InstallMethod::kUnknown);

  ASSERT_TRUE(base::WriteFile(install_type_path, "flexor"));
  got = GetInstallState(root_path);
  EXPECT_EQ(got.just_installed, true);
  EXPECT_EQ(got.method, InstallMethod::kFlexor);

  ASSERT_TRUE(base::WriteFile(install_type_path, "mass-deploy"));
  got = GetInstallState(root_path);
  EXPECT_EQ(got.just_installed, true);
  EXPECT_EQ(got.method, InstallMethod::kMassDeploy);

  ASSERT_TRUE(base::WriteFile(install_type_path, "remote-deploy"));
  got = GetInstallState(root_path);
  EXPECT_EQ(got.just_installed, true);
  EXPECT_EQ(got.method, InstallMethod::kRemoteDeploy);
}

// Test successful sends of the FRD install metric.
TEST(FlexFlexorInstallMetrics, SendFlexorInstallMetric_Success) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());
  base::FilePath root_path = root_dir.GetPath();

  StrictMock<MetricsLibraryMock> metrics;

  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexInstallMethod", _, 4))
      .WillRepeatedly(Return(true));

  // These pass without checking the existence of the file.
  InstallState expected =
      InstallState{.just_installed = true, .method = InstallMethod::kUnknown};
  EXPECT_TRUE(MaybeSendInstallMethodMetric(metrics, root_path, expected));
  expected =
      InstallState{.just_installed = false, .method = InstallMethod::kUnknown};
  EXPECT_TRUE(MaybeSendInstallMethodMetric(metrics, root_path, expected));

  const auto unencrypted_stateful_dir = root_dir.GetPath().Append(
      "mnt/stateful_partition/unencrypted/install_metrics");
  const auto install_type_path =
      unencrypted_stateful_dir.Append("install_type");
  ASSERT_TRUE(base::CreateDirectory(unencrypted_stateful_dir));
  ASSERT_TRUE(base::WriteFile(install_type_path, ""));

  expected =
      InstallState{.just_installed = true, .method = InstallMethod::kFlexor};
  EXPECT_TRUE(MaybeSendInstallMethodMetric(metrics, root_path, expected));
}

// Test failure cases of the FRD install metric.
TEST(FlexFlexorInstallMetrics, SendFlexorInstallMetric_Failure) {
  base::ScopedTempDir root_dir;
  CHECK(root_dir.CreateUniqueTempDir());
  base::FilePath root_path = root_dir.GetPath();

  StrictMock<MetricsLibraryMock> metrics;

  EXPECT_CALL(metrics, SendEnumToUMA("Platform.FlexInstallMethod", _, 4))
      .WillRepeatedly(Return(false));

  InstallState expected =
      InstallState{.just_installed = true, .method = InstallMethod::kFlexor};

  // No file to delete,
  EXPECT_FALSE(MaybeSendInstallMethodMetric(metrics, root_path, expected));

  const auto unencrypted_stateful_dir = root_dir.GetPath().Append(
      "mnt/stateful_partition/unencrypted/install_metrics");
  const auto install_type_path =
      unencrypted_stateful_dir.Append("install_type");
  ASSERT_TRUE(base::CreateDirectory(unencrypted_stateful_dir));
  ASSERT_TRUE(base::WriteFile(install_type_path, ""));

  // Now there's a file to delete, but SendEnum will fail.
  EXPECT_FALSE(MaybeSendInstallMethodMetric(metrics, root_path, expected));
}

struct StringToAttemptStatusTestParam {
  std::string input_string;
  FwupdLastAttemptStatus expected_result;
};

class StringToAttemptStatusTest
    : public testing::TestWithParam<StringToAttemptStatusTestParam> {
 protected:
  FwupdLastAttemptStatus test_result_;
};

TEST_P(StringToAttemptStatusTest, ValidConversionChecks) {
  const auto& param = GetParam();
  EXPECT_TRUE(StringToAttemptStatus(param.input_string, &test_result_));
  EXPECT_EQ(test_result_, param.expected_result);
}

INSTANTIATE_TEST_SUITE_P(
    StringToAttemptStatusTests,
    StringToAttemptStatusTest,
    testing::ValuesIn<StringToAttemptStatusTestParam>({
        {"0x0", FwupdLastAttemptStatus::kSuccess},
        {"0x1", FwupdLastAttemptStatus::kErrorUnsuccessful},
        {"0x2", FwupdLastAttemptStatus::kErrorInsufficientResources},
        {"0x3", FwupdLastAttemptStatus::kErrorIncorrectVersion},
        {"0x4", FwupdLastAttemptStatus::kErrorInvalidFormat},
        {"0x5", FwupdLastAttemptStatus::kErrorAuthError},
        {"0x6", FwupdLastAttemptStatus::kErrorPwrEvtAc},
        {"0x7", FwupdLastAttemptStatus::kErrorPwrEvtBatt},
        {"0x8", FwupdLastAttemptStatus::kErrorUnsatisfiedDependencies},
    }));

TEST(StringToAttemptStatusTest, InvalidConversionChecks) {
  FwupdLastAttemptStatus test_result_;

  // Not a hex number.
  EXPECT_FALSE(StringToAttemptStatus("10", &test_result_));

  // Greater than max hex number in enum.
  EXPECT_FALSE(StringToAttemptStatus("0x9", &test_result_));

  // Empty string
  EXPECT_FALSE(StringToAttemptStatus("", &test_result_));
}

TEST(ParseFwupdGetHistoryResponse, Valid) {
  const auto raw_devices = CreateValidRawDevices();

  const auto devices = ParseFwupdGetHistoryResponse(raw_devices);
  EXPECT_EQ(devices, CreateExpectedDeviceHistory());
}

TEST(ParseFwupdGetHistoryResponse, MissingField) {
  auto raw_devices = CreateValidRawDevices();
  raw_devices[0].erase(raw_devices[0].find("Name"));

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

TEST(ParseFwupdGetHistoryResponse, WrongFieldType) {
  auto raw_devices = CreateValidRawDevices();
  raw_devices[0].erase(raw_devices[0].find("Name"));
  raw_devices[0]["Name"] = 123;

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

TEST(ParseFwupdGetHistoryResponse, InvalidUpdateState) {
  auto raw_devices = CreateValidRawDevices();
  raw_devices[0].erase(raw_devices[0].find("UpdateState"));
  raw_devices[0]["UpdateState"] = static_cast<uint32_t>(123);

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

TEST(ParseFwupdGetHistoryResponse, ReleaseMissingMetadata) {
  auto raw_devices = CreateValidRawDevices();
  std::vector<brillo::VariantDictionary>* raw_releases =
      raw_devices[0]["Release"]
          .GetPtr<std::vector<brillo::VariantDictionary>>();
  brillo::VariantDictionary& raw_release = (*raw_releases)[0];
  raw_release.erase(raw_release.find("Metadata"));

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

TEST(ParseFwupdGetHistoryResponse, NonUefiInvalidRelease) {
  auto raw_devices = CreateValidRawDevices();
  raw_devices[0]["Plugin"] = std::string("not uefi");
  std::vector<brillo::VariantDictionary>* raw_releases =
      raw_devices[0]["Release"]
          .GetPtr<std::vector<brillo::VariantDictionary>>();
  brillo::VariantDictionary& raw_release = (*raw_releases)[0];
  raw_release.erase(raw_release.find("Metadata"));

  const auto devices = ParseFwupdGetHistoryResponse(raw_devices);

  auto expected_devices = CreateExpectedDeviceHistory();
  expected_devices[0].plugin = "not uefi";
  expected_devices[0].releases.clear();

  EXPECT_EQ(devices, expected_devices);
}

TEST(ParseFwupdGetHistoryResponse, ReleaseMetadataMissingStatus) {
  auto raw_devices = CreateValidRawDevices();
  std::vector<brillo::VariantDictionary>* raw_releases =
      raw_devices[0]["Release"]
          .GetPtr<std::vector<brillo::VariantDictionary>>();
  std::map<std::string, std::string>* raw_metadata =
      (*raw_releases)[0]["Metadata"]
          .GetPtr<std::map<std::string, std::string>>();
  raw_metadata->erase(raw_metadata->find("LastAttemptStatus"));

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

TEST(ParseFwupdGetHistoryResponse, ReleaseMetadataInvalidStatus) {
  auto raw_devices = CreateValidRawDevices();
  std::vector<brillo::VariantDictionary>* raw_releases =
      raw_devices[0]["Release"]
          .GetPtr<std::vector<brillo::VariantDictionary>>();
  std::map<std::string, std::string>* raw_metadata =
      (*raw_releases)[0]["Metadata"]
          .GetPtr<std::map<std::string, std::string>>();
  raw_metadata->erase(raw_metadata->find("LastAttemptStatus"));
  (*raw_metadata)["LastAttemptStatus"] = std::string("0x123");

  EXPECT_FALSE(ParseFwupdGetHistoryResponse(raw_devices).has_value());
}

class CallFwupdGetHistoryTest : public ::testing::Test {
 protected:
  CallFwupdGetHistoryTest()
      : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}),
        mock_object_(new dbus::MockObjectProxy(
            mock_bus_.get(), "mock-fwupd-service", dbus::ObjectPath("/"))) {}

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_;
};

TEST_F(CallFwupdGetHistoryTest, GetValidHistory) {
  EXPECT_CALL(*mock_object_, CallMethodAndBlock)
      .WillOnce(Return(ByMove(base::ok(CreateValidGetHistoryResponse()))));

  EXPECT_EQ(CallFwupdGetHistory(mock_object_.get()),
            CreateExpectedDeviceHistory());
}

TEST_F(CallFwupdGetHistoryTest, GetEmptyHistory) {
  EXPECT_CALL(*mock_object_, CallMethodAndBlock)
      .WillOnce(Return(ByMove(base::unexpected(dbus::Error(
          std::string(kFwupdGetHistoryNothingToDo), "No history")))));

  EXPECT_EQ(CallFwupdGetHistory(mock_object_.get()),
            std::vector<FwupdDeviceHistory>());
}

TEST_F(CallFwupdGetHistoryTest, Error) {
  EXPECT_CALL(*mock_object_, CallMethodAndBlock)
      .WillOnce(
          Return(ByMove(base::unexpected(dbus::Error("uh oh", "failed")))));

  EXPECT_FALSE(CallFwupdGetHistory(mock_object_.get()).has_value());
}

TEST(GetAndUpdateFwupMetricTimestamp, Success) {
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());
  base::FilePath path = temp_dir.GetPath().Append("last_fwup_report");

  const auto timestamp2 = base::Time::UnixEpoch() + base::Seconds(123);
  const auto timestamp3 = base::Time::UnixEpoch() + base::Seconds(456);

  // The file doesn't exist, so the first read should return the default
  // `UnixEpoch` value.
  EXPECT_EQ(GetAndUpdateFwupMetricTimestamp(timestamp2, path),
            base::Time::UnixEpoch());

  // The second read should return the just-written timestamp.
  EXPECT_EQ(GetAndUpdateFwupMetricTimestamp(timestamp3, path), timestamp2);
}

TEST(GetAndUpdateFwupMetricTimestamp, WriteFail) {
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());
  base::FilePath path =
      temp_dir.GetPath().Append("does_not_exist/last_fwup_report");

  // The directory doesn't exist, so the write will fail.
  EXPECT_FALSE(GetAndUpdateFwupMetricTimestamp(base::Time::UnixEpoch(), path)
                   .has_value());
}

TEST(GetAndUpdateFwupMetricTimestamp, InvalidTimestamp) {
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());
  base::FilePath path = temp_dir.GetPath().Append("last_fwup_report");
  CHECK(base::WriteFile(path, "invalid"));

  // The timestamp cannot be parsed.
  EXPECT_FALSE(GetAndUpdateFwupMetricTimestamp(base::Time::UnixEpoch(), path)
                   .has_value());
}

TEST(FlexFwupHistoryMetrics, SendAttemptStatusAsMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  FwupdDeviceHistory history;
  history.name = "test_device";
  history.update_state = FwupdUpdateState::kFailed;
  FwupdRelease release;
  release.last_attempt_status = FwupdLastAttemptStatus::kErrorUnsuccessful;
  history.releases.push_back(release);

  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(AttemptStatusToUpdateResult(
                                       history.releases[0].last_attempt_status)
                                       .value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendFwupMetric(metrics, history));
}

TEST(FlexFwupHistoryMetrics, SendMultipleAttemptStatusesAsMetrics) {
  StrictMock<MetricsLibraryMock> metrics;
  FwupdDeviceHistory history;
  history.name = "test_device";
  history.update_state = FwupdUpdateState::kFailed;
  FwupdRelease first_release;
  first_release.last_attempt_status =
      FwupdLastAttemptStatus::kErrorUnsuccessful;
  history.releases.push_back(first_release);
  FwupdRelease second_release;
  second_release.last_attempt_status =
      FwupdLastAttemptStatus::kErrorIncorrectVersion;
  history.releases.push_back(second_release);

  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(AttemptStatusToUpdateResult(
                                       history.releases[0].last_attempt_status)
                                       .value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));

  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(AttemptStatusToUpdateResult(
                                       history.releases[1].last_attempt_status)
                                       .value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendFwupMetric(metrics, history));
}

TEST(FlexFwupHistoryMetrics, SendUpdateStateAsMetric) {
  StrictMock<MetricsLibraryMock> metrics;
  FwupdDeviceHistory history;
  history.name = "test_device";
  history.update_state = FwupdUpdateState::kSuccess;

  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(
                      UpdateStateToUpdateResult(history.update_state).value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendFwupMetric(metrics, history));
}

TEST(FlexFwupHistoryMetrics, LastAttemptStatusIgnoredOnNonFailingUpdates) {
  StrictMock<MetricsLibraryMock> metrics;
  FwupdDeviceHistory history;
  history.name = "test_device";
  history.update_state = FwupdUpdateState::kSuccess;
  FwupdRelease release;

  // kErrorUnsuccessful indicates failure, however this should be ignored as
  // the update state is successful.
  release.last_attempt_status = FwupdLastAttemptStatus::kErrorUnsuccessful;
  history.releases.push_back(release);

  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(
                      UpdateStateToUpdateResult(history.update_state).value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics,
              SendEnumToUMA(
                  "Platform.FlexUefiCapsuleUpdateResult",
                  static_cast<int>(AttemptStatusToUpdateResult(
                                       history.releases[0].last_attempt_status)
                                       .value()),
                  static_cast<int>(UpdateResult::kMaxValue) + 1))
      .Times(0);

  EXPECT_TRUE(SendFwupMetric(metrics, history));
}

TEST(SendFwupMetrics, UefiSuccess) {
  StrictMock<MetricsLibraryMock> metrics;
  auto devices = CreateExpectedDeviceHistory();
  devices[0].plugin = kUefiCapsulePlugin;

  EXPECT_CALL(metrics,
              SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                            static_cast<int>(UpdateResult::kSuccess),
                            static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(true));

  EXPECT_TRUE(SendFwupMetrics(metrics, devices, base::Time::UnixEpoch()));
}

TEST(SendFwupMetrics, SkipNonUefi) {
  StrictMock<MetricsLibraryMock> metrics;
  auto devices = CreateExpectedDeviceHistory();
  devices[0].plugin = std::string("turtle");

  EXPECT_TRUE(SendFwupMetrics(metrics, devices, base::Time::UnixEpoch()));
}

TEST(SendFwupMetrics, SkipOldReport) {
  StrictMock<MetricsLibraryMock> metrics;
  auto devices = CreateExpectedDeviceHistory();
  devices[0].plugin = kUefiCapsulePlugin;

  EXPECT_TRUE(SendFwupMetrics(metrics, devices,
                              base::Time::UnixEpoch() + base::Seconds(100)));
}

TEST(SendFwupMetrics, Error) {
  StrictMock<MetricsLibraryMock> metrics;
  auto devices = CreateExpectedDeviceHistory();
  devices[0].plugin = kUefiCapsulePlugin;

  EXPECT_CALL(metrics,
              SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                            static_cast<int>(UpdateResult::kSuccess),
                            static_cast<int>(UpdateResult::kMaxValue) + 1))
      .WillOnce(Return(false));

  EXPECT_FALSE(SendFwupMetrics(metrics, devices, base::Time::UnixEpoch()));
}

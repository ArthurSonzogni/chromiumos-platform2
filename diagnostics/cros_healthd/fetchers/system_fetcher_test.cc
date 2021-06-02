// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

// Fake lsb-release values used for testing.
const char kFakeReleaseMilestone[] = "87";
const char kFakeBuildNumber[] = "13544";
const char kFakePatchNumber[] = "59.0";
const char kFakeReleaseChannel[] = "stable-channel";
// Fake cached VPD values used for testing.
const char kFakeFirstPowerDate[] = "2020-40";
const char kFakeManufactureDate[] = "2019-01-01";
const char kFakeSkuNumber[] = "ABCD&^A";
const char kFakeProductSerialNumber[] = "8607G03EDF";
const char kFakeProductModelName[] = "XX ModelName 007 XY";
// Fake CrosConfig values used for testing.
constexpr char kFakeMarketingName[] = "Latitude 1234 Chromebook Enterprise";
constexpr char kFakeProductName[] = "ProductName";
// Fake DMI values used for testing.
constexpr char kFakeBiosVersion[] = "Google_BoardName.12200.68.0";
constexpr char kFakeBoardName[] = "BoardName";
constexpr char kFakeBoardVersion[] = "rev1234";
constexpr uint64_t kFakeChassisTypeOutput = 9;

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

template <typename T>
base::Optional<std::string> GetMockValue(const T& value) {
  return value;
}

base::Optional<std::string> GetMockValue(
    const mojo_ipc::NullableUint64Ptr& ptr) {
  if (ptr)
    return std::to_string(ptr->value);
  return base::nullopt;
}

class SystemUtilsTest : public BaseFileTest {
 protected:
  SystemUtilsTest() = default;
  SystemUtilsTest(const SystemUtilsTest&) = delete;
  SystemUtilsTest& operator=(const SystemUtilsTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    SetTestRoot(mock_context_.root_dir());

    relative_vpd_rw_dir_ = root_dir().Append(kRelativeVpdRwPath);
    relative_vpd_ro_dir_ = root_dir().Append(kRelativeVpdRoPath);
    relative_dmi_info_path_ = root_dir().Append(kRelativeDmiInfoPath);

    expected_system_info_ = mojo_ipc::SystemInfo::New();
    expected_system_info_->first_power_date = "2020-40";
    expected_system_info_->manufacture_date = "2019-01-01";
    expected_system_info_->product_sku_number = "ABCD&^A";
    expected_system_info_->product_serial_number = "8607G03EDF";
    expected_system_info_->product_model_name = "XX ModelName 007 XY";
    expected_system_info_->marketing_name =
        "Latitude 1234 Chromebook Enterprise";
    expected_system_info_->bios_version = "Google_BoardName.12200.68.0";
    expected_system_info_->board_name = "BoardName";
    expected_system_info_->board_version = "rev1234";
    expected_system_info_->chassis_type = mojo_ipc::NullableUint64::New(9);
    expected_system_info_->product_name = "ProductName";
    expected_system_info_->os_version = mojo_ipc::OsVersion::New();
    expected_system_info_->os_version->release_milestone = "87";
    expected_system_info_->os_version->build_number = "13544";
    expected_system_info_->os_version->patch_number = "59.0";
    expected_system_info_->os_version->release_channel = "stable-channel";

    SetSystemInfo(expected_system_info_);
    SetHasSkuNumber(true);
  }

  void SetSystemInfo(const mojo_ipc::SystemInfoPtr& system_info) {
    SetMockFile({kRelativeVpdRwPath, kFirstPowerDateFileName},
                system_info->first_power_date);
    SetMockFile({kRelativeVpdRoPath, kManufactureDateFileName},
                system_info->manufacture_date);
    SetMockFile({kRelativeVpdRoPath, kSkuNumberFileName},
                system_info->product_sku_number);
    SetMockFile({kRelativeVpdRoPath, kProductSerialNumberFileName},
                system_info->product_serial_number);
    SetMockFile({kRelativeVpdRoPath, kProductModelNameFileName},
                system_info->product_model_name);
    // Currently, cros_config never returns base::nullopt.
    mock_context_.fake_system_config()->SetMarketingName(
        system_info->marketing_name);
    ASSERT_TRUE(system_info->product_name.has_value());
    mock_context_.fake_system_config()->SetProductName(
        system_info->product_name.value());

    SetMockFile({kRelativeDmiInfoPath, kBiosVersionFileName},
                system_info->bios_version);
    SetMockFile({kRelativeDmiInfoPath, kBoardNameFileName},
                system_info->board_name);
    SetMockFile({kRelativeDmiInfoPath, kBoardVersionFileName},
                system_info->board_version);
    SetMockFile({kRelativeDmiInfoPath, kChassisTypeFileName},
                system_info->chassis_type);
    SetOsVersion(system_info->os_version);
  }

  void SetOsVersion(const mojo_ipc::OsVersionPtr& os_version) {
    PopulateLsbRelease(base::StringPrintf(
        "CHROMEOS_RELEASE_CHROME_MILESTONE=%s\n"
        "CHROMEOS_RELEASE_BUILD_NUMBER=%s\n"
        "CHROMEOS_RELEASE_PATCH_NUMBER=%s\n"
        "CHROMEOS_RELEASE_TRACK=%s\n",
        os_version->release_milestone.c_str(), os_version->build_number.c_str(),
        os_version->patch_number.c_str(), os_version->release_channel.c_str()));
  }

  // Sets the mock file with |value|. If the |value| is omitted, deletes the
  // file.
  template <typename T>
  void SetMockFile(const PathType& path, const T& value) {
    auto mock = GetMockValue(value);
    if (mock) {
      SetFile(path, mock.value());
    } else {
      UnsetPath(path);
    }
  }

  chromeos::cros_healthd::mojom::SystemResultPtr FetchSystemInfo() {
    return system_fetcher_.FetchSystemInfo();
  }

  void SetHasSkuNumber(bool val) {
    mock_context_.fake_system_config()->SetHasSkuNumber(val);
  }

  void PopulateLsbRelease(const std::string& lsb_release) {
    // Deletes the old instance to release the global lock before creating a new
    // instance.
    chromeos_version_.reset();
    chromeos_version_ = std::make_unique<base::test::ScopedChromeOSVersionInfo>(
        lsb_release, base::Time::Now());
  }

  void ValidateCachedVpdInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    ASSERT_TRUE(system_info->first_power_date.has_value());
    EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
    ASSERT_TRUE(system_info->manufacture_date.has_value());
    EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
    ASSERT_TRUE(system_info->product_sku_number.has_value());
    EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
    ASSERT_TRUE(system_info->product_serial_number.has_value());
    EXPECT_EQ(system_info->product_serial_number.value(),
              kFakeProductSerialNumber);
    ASSERT_TRUE(system_info->product_model_name.has_value());
    EXPECT_EQ(system_info->product_model_name.value(), kFakeProductModelName);
  }

  void ValidateCrosConfigInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    EXPECT_EQ(system_info->marketing_name, kFakeMarketingName);
    EXPECT_EQ(system_info->product_name, kFakeProductName);
  }

  void ValidateDmiInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    ASSERT_TRUE(system_info->bios_version.has_value());
    EXPECT_EQ(system_info->bios_version, kFakeBiosVersion);
    ASSERT_TRUE(system_info->board_name.has_value());
    EXPECT_EQ(system_info->board_name, kFakeBoardName);
    ASSERT_TRUE(system_info->board_version.has_value());
    EXPECT_EQ(system_info->board_version, kFakeBoardVersion);
    ASSERT_TRUE(system_info->chassis_type);
    EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
  }

  void ValidateOsVersion(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    EXPECT_EQ(system_info->os_version->release_milestone,
              kFakeReleaseMilestone);
    EXPECT_EQ(system_info->os_version->build_number, kFakeBuildNumber);
    EXPECT_EQ(system_info->os_version->patch_number, kFakePatchNumber);
    EXPECT_EQ(system_info->os_version->release_channel, kFakeReleaseChannel);
  }

  const base::FilePath& relative_vpd_rw_dir() { return relative_vpd_rw_dir_; }

  const base::FilePath& relative_vpd_ro_dir() { return relative_vpd_ro_dir_; }

  const base::FilePath& relative_dmi_info_path() {
    return relative_dmi_info_path_;
  }

 protected:
  mojo_ipc::SystemInfoPtr expected_system_info_;

 private:
  MockContext mock_context_;
  SystemFetcher system_fetcher_{&mock_context_};
  base::FilePath relative_vpd_rw_dir_;
  base::FilePath relative_vpd_ro_dir_;
  base::FilePath relative_dmi_info_path_;
  std::unique_ptr<base::test::ScopedChromeOSVersionInfo> chromeos_version_;
};

// Test that we can read the system info, when it exists.
TEST_F(SystemUtilsTest, TestFetchSystemInfo) {
  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that no first_power_date is reported when |kFirstPowerDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoFirstPowerDate) {
  // Delete the file containing first power date.
  ASSERT_TRUE(
      base::DeleteFile(relative_vpd_rw_dir().Append(kFirstPowerDateFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that cached VPD values except first power date are obtained.
  EXPECT_FALSE(system_info->first_power_date.has_value());
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
  ASSERT_TRUE(system_info->product_serial_number.has_value());
  EXPECT_EQ(system_info->product_serial_number.value(),
            kFakeProductSerialNumber);
  ASSERT_TRUE(system_info->product_model_name.has_value());
  EXPECT_EQ(system_info->product_model_name.value(), kFakeProductModelName);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that no manufacture_date is reported when |kManufactureDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoManufactureDate) {
  // Delete the file containing manufacture date.
  ASSERT_TRUE(
      base::DeleteFile(relative_vpd_ro_dir().Append(kManufactureDateFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that cached VPD values except manufacture date are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  EXPECT_FALSE(system_info->manufacture_date.has_value());
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
  ASSERT_TRUE(system_info->product_serial_number.has_value());
  EXPECT_EQ(system_info->product_serial_number.value(),
            kFakeProductSerialNumber);
  ASSERT_TRUE(system_info->product_model_name.has_value());
  EXPECT_EQ(system_info->product_model_name.value(), kFakeProductModelName);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that reading system info that does not have |kSkuNumberFileName| (when
// it should) reports an error.
TEST_F(SystemUtilsTest, TestSkuNumberError) {
  // Delete the file containing sku number.
  ASSERT_TRUE(
      base::DeleteFile(relative_vpd_ro_dir().Append(kSkuNumberFileName)));

  // Confirm that an error is obtained.
  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

// Test that no product_sku_number is returned when the device does not have
// |kSkuNumberFileName|.
TEST_F(SystemUtilsTest, TestNoSkuNumber) {
  // Delete the file containing sku number.
  ASSERT_TRUE(
      base::DeleteFile(relative_vpd_ro_dir().Append(kSkuNumberFileName)));
  // Ensure that there is no sku number.
  SetHasSkuNumber(false);

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that correct cached VPD values except sku number are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  EXPECT_FALSE(system_info->product_sku_number.has_value());
  ASSERT_TRUE(system_info->product_serial_number.has_value());
  EXPECT_EQ(system_info->product_serial_number.value(),
            kFakeProductSerialNumber);
  ASSERT_TRUE(system_info->product_model_name.has_value());
  EXPECT_EQ(system_info->product_model_name.value(), kFakeProductModelName);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that no product_serial_number is returned when the device does not have
// |kProductSerialNumberFileName|.
TEST_F(SystemUtilsTest, TestNoProductSerialNumber) {
  // Delete the file containing serial number.
  ASSERT_TRUE(base::DeleteFile(
      relative_vpd_ro_dir().Append(kProductSerialNumberFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that correct cached VPD values except serial number are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
  EXPECT_FALSE(system_info->product_serial_number.has_value());
  ASSERT_TRUE(system_info->product_model_name.has_value());
  EXPECT_EQ(system_info->product_model_name.value(), kFakeProductModelName);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that no product_model_name is returned when the device does not have
// |kProductModelNameFileName|.
TEST_F(SystemUtilsTest, TestNoProductModelName) {
  // Delete the file containing model name.
  ASSERT_TRUE(base::DeleteFile(
      relative_vpd_ro_dir().Append(kProductModelNameFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that correct cached VPD values except serial number are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
  ASSERT_TRUE(system_info->product_serial_number.has_value());
  EXPECT_EQ(system_info->product_serial_number.value(),
            kFakeProductSerialNumber);
  EXPECT_FALSE(system_info->product_model_name.has_value());

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
  ValidateOsVersion(system_info);
}

// Test that no DMI fields are populated when |kRelativeDmiInfoPath| doesn't
// exist.
TEST_F(SystemUtilsTest, TestNoSysDevicesVirtualDmiId) {
  // Delete the directory |kRelativeDmiInfoPath|.
  ASSERT_TRUE(base::DeletePathRecursively(relative_dmi_info_path()));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateOsVersion(system_info);

  // Confirm that no DMI values are obtained.
  EXPECT_FALSE(system_info->bios_version.has_value());
  EXPECT_FALSE(system_info->board_name.has_value());
  EXPECT_FALSE(system_info->board_version.has_value());
  EXPECT_FALSE(system_info->chassis_type);
}

// Test that there is no bios_version when |kBiosVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBiosVersion) {
  // Delete the file containing bios version.
  ASSERT_TRUE(
      base::DeleteFile(relative_dmi_info_path().Append(kBiosVersionFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateOsVersion(system_info);

  // Confirm that the bios_version was not populated.
  EXPECT_FALSE(system_info->bios_version.has_value());
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
}

// Test that there is no board_name when |kBoardNameFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardName) {
  // Delete the file containing board name.
  ASSERT_TRUE(
      base::DeleteFile(relative_dmi_info_path().Append(kBoardNameFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateOsVersion(system_info);

  // Confirm that the board_name was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  EXPECT_FALSE(system_info->board_name.has_value());
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
}

// Test that there is no board_version when |kBoardVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardVersion) {
  // Delete the file containing board version.
  ASSERT_TRUE(
      base::DeleteFile(relative_dmi_info_path().Append(kBoardVersionFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateOsVersion(system_info);

  // Confirm that the board_version was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  EXPECT_FALSE(system_info->board_version.has_value());
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
}

// Test that there is no chassis_type when |kChassisTypeFileName| is missing.
TEST_F(SystemUtilsTest, TestNoChassisType) {
  // Delete the file containing chassis type.
  ASSERT_TRUE(
      base::DeleteFile(relative_dmi_info_path().Append(kChassisTypeFileName)));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateOsVersion(system_info);

  // Confirm that the chassis_type was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  EXPECT_FALSE(system_info->chassis_type);
}

// Test that reading a chassis_type that cannot be converted to an unsigned
// integer reports an error.
TEST_F(SystemUtilsTest, TestBadChassisType) {
  // Overwrite the contents of |kChassisTypeFileName| with a chassis_type value
  // that cannot be parsed into an unsigned integer.
  std::string bad_chassis_type = "bad chassis type";
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      relative_dmi_info_path().Append(kChassisTypeFileName), bad_chassis_type));

  // Confirm that an error is obtained.
  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Tests that an error is returned if there is no OS version information
// populated in lsb-release.
TEST_F(SystemUtilsTest, TestNoOsVersion) {
  PopulateLsbRelease("");

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

// Tests that an error is returned if the lsb-release file is malformed.
TEST_F(SystemUtilsTest, TestBadOsVersion) {
  PopulateLsbRelease(
      base::StringPrintf("Milestone%s\n"
                         "CHROMEOS_RELEASE_BUILD_NUMBER=%s\n"
                         "CHROMEOS_RELEASE_PATCH_NUMBER=%s\n"
                         "CHROMEOS_RELEASE_TRACK=%s\n",
                         kFakeReleaseMilestone, kFakeBuildNumber,
                         kFakePatchNumber, kFakeReleaseChannel));

  auto system_result = FetchSystemInfo();
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

}  // namespace
}  // namespace diagnostics

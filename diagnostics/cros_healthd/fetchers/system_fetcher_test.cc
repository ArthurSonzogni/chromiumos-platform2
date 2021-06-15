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
#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

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
    SetMockFile({kRelativePathVpdRw, kFileNameActivateDate},
                system_info->first_power_date);
    SetMockFile({kRelativePathVpdRo, kFileNameMfgDate},
                system_info->manufacture_date);
    SetMockFile({kRelativePathVpdRo, kFileNameSkuNumber},
                system_info->product_sku_number);
    SetMockFile({kRelativePathVpdRo, kFileNameSerialNumber},
                system_info->product_serial_number);
    SetMockFile({kRelativePathVpdRo, kFileNameModelName},
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

  void ExpectFetchSystemInfo() {
    auto system_result = system_fetcher_.FetchSystemInfo();
    ASSERT_TRUE(system_result->is_system_info());
    EXPECT_EQ(system_result->get_system_info(), expected_system_info_);
  }

  void ExpectFetchProbeError(const mojo_ipc::ErrorType& expected) {
    auto system_result = system_fetcher_.FetchSystemInfo();
    ASSERT_TRUE(system_result->is_error());
    EXPECT_EQ(system_result->get_error()->type, expected);
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
  ExpectFetchSystemInfo();
}

// Test that no first_power_date is reported when |kFirstPowerDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoFirstPowerDate) {
  expected_system_info_->first_power_date = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that no manufacture_date is reported when |kManufactureDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoManufactureDate) {
  expected_system_info_->manufacture_date = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that reading system info that does not have |kSkuNumberFileName| (when
// it should) reports an error.
TEST_F(SystemUtilsTest, TestSkuNumberError) {
  expected_system_info_->product_sku_number = base::nullopt;
  SetSystemInfo(expected_system_info_);
  // Confirm that an error is obtained.
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

TEST_F(SystemUtilsTest, TestNoSkuNumber) {
  // Sku number file exists.
  SetSystemInfo(expected_system_info_);
  // Ensure that there is no sku number returned even if sku number exists.
  SetHasSkuNumber(false);
  expected_system_info_->product_sku_number = base::nullopt;
  ExpectFetchSystemInfo();

  // Sku number file doesn't exist.
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that no product_serial_number is returned when the device does not have
// |kProductSerialNumberFileName|.
TEST_F(SystemUtilsTest, TestNoProductSerialNumber) {
  expected_system_info_->product_serial_number = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that no product_model_name is returned when the device does not have
// |kProductModelNameFileName|.
TEST_F(SystemUtilsTest, TestNoProductModelName) {
  expected_system_info_->product_model_name = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that no DMI fields are populated when |kRelativeDmiInfoPath| doesn't
// exist.
TEST_F(SystemUtilsTest, TestNoSysDevicesVirtualDmiId) {
  expected_system_info_->bios_version = base::nullopt;
  expected_system_info_->board_name = base::nullopt;
  expected_system_info_->board_version = base::nullopt;
  expected_system_info_->chassis_type = nullptr;
  // Delete the whole directory |kRelativeDmiInfoPath|.
  UnsetPath(kRelativeDmiInfoPath);
  ExpectFetchSystemInfo();
}

// Test that there is no bios_version when |kBiosVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBiosVersion) {
  expected_system_info_->bios_version = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that there is no board_name when |kBoardNameFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardName) {
  expected_system_info_->board_name = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that there is no board_version when |kBoardVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardVersion) {
  expected_system_info_->board_version = base::nullopt;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that there is no chassis_type when |kChassisTypeFileName| is missing.
TEST_F(SystemUtilsTest, TestNoChassisType) {
  expected_system_info_->chassis_type = nullptr;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that reading a chassis_type that cannot be converted to an unsigned
// integer reports an error.
TEST_F(SystemUtilsTest, TestBadChassisType) {
  // Overwrite the contents of |kChassisTypeFileName| with a chassis_type value
  // that cannot be parsed into an unsigned integer.
  std::string bad_chassis_type = "bad chassis type";
  SetMockFile({kRelativeDmiInfoPath, kChassisTypeFileName}, bad_chassis_type);
  ExpectFetchProbeError(mojo_ipc::ErrorType::kParseError);
}

// Tests that an error is returned if there is no OS version information
// populated in lsb-release.
TEST_F(SystemUtilsTest, TestNoOsVersion) {
  PopulateLsbRelease("");
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

// Tests that an error is returned if the lsb-release file is malformed.
TEST_F(SystemUtilsTest, TestBadOsVersion) {
  PopulateLsbRelease(
      "Milestone\n"
      "CHROMEOS_RELEASE_BUILD_NUMBER=1\n"
      "CHROMEOS_RELEASE_PATCH_NUMBER=2\n"
      "CHROMEOS_RELEASE_TRACK=3\n");
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

}  // namespace
}  // namespace diagnostics

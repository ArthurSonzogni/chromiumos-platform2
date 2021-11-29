// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_type_utils.h"
#include "diagnostics/cros_healthd/executor/mock_executor_adapter.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

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

void OnGetSystemInfoResponse(mojo_ipc::SystemResultPtr* response_update,
                             mojo_ipc::SystemResultPtr response) {
  *response_update = std::move(response);
}

void OnGetSystemInfoV2Response(mojo_ipc::SystemResultV2Ptr* response_update,
                               mojo_ipc::SystemResultV2Ptr response) {
  *response_update = std::move(response);
}

class SystemUtilsTest : public BaseFileTest {
 protected:
  SystemUtilsTest() = default;
  SystemUtilsTest(const SystemUtilsTest&) = delete;
  SystemUtilsTest& operator=(const SystemUtilsTest&) = delete;

  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());

    expected_system_info_ = mojo_ipc::SystemInfoV2::New();
    auto& vpd_info = expected_system_info_->vpd_info;
    vpd_info = mojo_ipc::VpdInfo::New();
    vpd_info->activate_date = "2020-40";
    vpd_info->mfg_date = "2019-01-01";
    vpd_info->model_name = "XX ModelName 007 XY";
    vpd_info->region = "us";
    vpd_info->serial_number = "8607G03EDF";
    vpd_info->sku_number = "ABCD&^A";
    auto& dmi_info = expected_system_info_->dmi_info;
    dmi_info = mojo_ipc::DmiInfo::New();
    dmi_info->bios_vendor = "Google";
    dmi_info->bios_version = "Google_BoardName.12200.68.0";
    dmi_info->board_name = "BoardName";
    dmi_info->board_vendor = "Google";
    dmi_info->board_version = "rev1234";
    dmi_info->chassis_vendor = "Google";
    dmi_info->chassis_type = mojo_ipc::NullableUint64::New(9);
    dmi_info->product_family = "FooFamily";
    dmi_info->product_name = "BarProductName";
    dmi_info->product_version = "rev1234";
    dmi_info->sys_vendor = "Google";
    auto& os_info = expected_system_info_->os_info;
    os_info = mojo_ipc::OsInfo::New();
    os_info->code_name = "CodeName";
    os_info->marketing_name = "Latitude 1234 Chromebook Enterprise";
    os_info->boot_mode = mojo_ipc::BootMode::kCrosSecure;
    auto& os_version = os_info->os_version;
    os_version = mojo_ipc::OsVersion::New();
    os_version->release_milestone = "87";
    os_version->build_number = "13544";
    os_version->patch_number = "59.0";
    os_version->release_channel = "stable-channel";

    SetSystemInfo(expected_system_info_);
    SetHasSkuNumber(true);
  }

  void SetSystemInfo(const mojo_ipc::SystemInfoV2Ptr& system_info) {
    SetVpdInfo(system_info->vpd_info);
    SetDmiInfo(system_info->dmi_info);
    SetOsInfo(system_info->os_info);
  }

  void SetVpdInfo(const mojo_ipc::VpdInfoPtr& vpd_info) {
    const auto& ro = kRelativePathVpdRo;
    const auto& rw = kRelativePathVpdRw;
    if (vpd_info.is_null()) {
      // Delete the whole vpd dir.
      UnsetPath(base::FilePath(ro).DirName());
      return;
    }
    SetMockFile({rw, kFileNameActivateDate}, vpd_info->activate_date);
    SetMockFile({ro, kFileNameMfgDate}, vpd_info->mfg_date);
    SetMockFile({ro, kFileNameModelName}, vpd_info->model_name);
    SetMockFile({ro, kFileNameRegion}, vpd_info->region);
    SetMockFile({ro, kFileNameSerialNumber}, vpd_info->serial_number);
    SetMockFile({ro, kFileNameSkuNumber}, vpd_info->sku_number);
  }

  void SetDmiInfo(const mojo_ipc::DmiInfoPtr& dmi_info) {
    const auto& dmi = kRelativePathDmiInfo;
    if (dmi_info.is_null()) {
      UnsetPath(dmi);
      return;
    }
    SetMockFile({dmi, kFileNameBiosVendor}, dmi_info->bios_vendor);
    SetMockFile({dmi, kFileNameBiosVersion}, dmi_info->bios_version);
    SetMockFile({dmi, kFileNameBoardName}, dmi_info->board_name);
    SetMockFile({dmi, kFileNameBoardVendor}, dmi_info->board_vendor);
    SetMockFile({dmi, kFileNameBoardVersion}, dmi_info->board_version);
    SetMockFile({dmi, kFileNameChassisVendor}, dmi_info->chassis_vendor);
    SetMockFile({dmi, kFileNameChassisType}, dmi_info->chassis_type);
    SetMockFile({dmi, kFileNameProductFamily}, dmi_info->product_family);
    SetMockFile({dmi, kFileNameProductName}, dmi_info->product_name);
    SetMockFile({dmi, kFileNameProductVersion}, dmi_info->product_version);
    SetMockFile({dmi, kFileNameSysVendor}, dmi_info->sys_vendor);
  }

  void SetOsInfo(const mojo_ipc::OsInfoPtr& os_info) {
    ASSERT_FALSE(os_info.is_null());
    mock_context_.fake_system_config()->SetMarketingName(
        os_info->marketing_name);
    mock_context_.fake_system_config()->SetCodeName(os_info->code_name);
    SetOsVersion(os_info->os_version);
    SetBootModeInProcCmd(os_info->boot_mode);
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

  void SetBootModeInProcCmd(const mojo_ipc::BootMode& boot_mode) {
    base::Optional<std::string> proc_cmd;
    switch (boot_mode) {
      case mojo_ipc::BootMode::kCrosSecure:
        proc_cmd = "Foo Bar=1 cros_secure Foo Bar=1";
        break;
      case mojo_ipc::BootMode::kCrosEfi:
        proc_cmd = "Foo Bar=1 cros_efi Foo Bar=1";
        break;
      case mojo_ipc::BootMode::kCrosEfiSecure:
        proc_cmd = "Foo Bar=1 cros_efi Foo Bar=1";
        break;
      case mojo_ipc::BootMode::kCrosLegacy:
        proc_cmd = "Foo Bar=1 cros_legacy Foo Bar=1";
        break;
      case mojo_ipc::BootMode::kUnknown:
        proc_cmd = "Foo Bar=1 Foo Bar=1";
        break;
    }
    SetMockFile(kFilePathProcCmdline, proc_cmd);
  }

  void SetUEFISceureBootResponse(const std::string& content) {
    // Set the mock executor response.
    EXPECT_CALL(*mock_executor(), GetUEFISecureBootContent(_))
        .Times(2)
        .WillRepeatedly(WithArg<0>(Invoke(
            [content](executor_ipc::Executor::GetUEFISecureBootContentCallback
                          callback) { std::move(callback).Run(content); })));
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
    auto system_result = FetchSystemInfoV2();
    ASSERT_FALSE(system_result.is_null());
    ASSERT_FALSE(system_result->is_error());
    ASSERT_TRUE(system_result->is_system_info_v2());
    auto res = std::move(system_result->get_system_info_v2());
    EXPECT_EQ(res, expected_system_info_)
        << GetDiffString(res, expected_system_info_);

    auto system_result_old = FetchSystemInfo();
    ASSERT_FALSE(system_result_old.is_null());
    ASSERT_FALSE(system_result_old->is_error());
    ASSERT_TRUE(system_result_old->is_system_info());
    EXPECT_EQ(system_result_old->get_system_info(),
              SystemFetcher::ConvertToSystemInfo(expected_system_info_));
  }

  void ExpectFetchProbeError(const mojo_ipc::ErrorType& expected) {
    auto system_result = FetchSystemInfo();
    ASSERT_TRUE(system_result->is_error());
    EXPECT_EQ(system_result->get_error()->type, expected);
  }

 protected:
  mojo_ipc::SystemInfoV2Ptr expected_system_info_;
  MockExecutorAdapter* mock_executor() { return mock_context_.mock_executor(); }
  mojo_ipc::SystemResultPtr FetchSystemInfo() {
    base::RunLoop run_loop;
    mojo_ipc::SystemResultPtr result;
    system_fetcher_.FetchSystemInfo(
        base::BindOnce(&OnGetSystemInfoResponse, &result));
    run_loop.RunUntilIdle();
    return result;
  }
  mojo_ipc::SystemResultV2Ptr FetchSystemInfoV2() {
    base::RunLoop run_loop;
    mojo_ipc::SystemResultV2Ptr result;
    system_fetcher_.FetchSystemInfoV2(
        base::BindOnce(&OnGetSystemInfoV2Response, &result));
    run_loop.RunUntilIdle();
    return result;
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  SystemFetcher system_fetcher_{&mock_context_};
  base::FilePath relative_vpd_rw_dir_;
  base::FilePath relative_vpd_ro_dir_;
  base::FilePath relative_dmi_info_path_;
  std::unique_ptr<base::test::ScopedChromeOSVersionInfo> chromeos_version_;
};

// Template for testing the missing field of vpd/dmi.
#define TEST_MISSING_FIELD(info, field)                 \
  TEST_F(SystemUtilsTest, TestNo_##info##_##field) {    \
    expected_system_info_->info->field = base::nullopt; \
    SetSystemInfo(expected_system_info_);               \
    ExpectFetchSystemInfo();                            \
  }

TEST_F(SystemUtilsTest, TestFetchSystemInfo) {
  ExpectFetchSystemInfo();
}

TEST_F(SystemUtilsTest, TestNoVpdDir) {
  expected_system_info_->vpd_info = nullptr;
  SetSystemInfo(expected_system_info_);
  SetHasSkuNumber(true);
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);

  SetHasSkuNumber(false);
  ExpectFetchSystemInfo();
}

TEST_F(SystemUtilsTest, TestNoSkuNumber) {
  // Sku number file exists.
  SetSystemInfo(expected_system_info_);
  // Ensure that there is no sku number returned even if sku number exists.
  SetHasSkuNumber(false);
  expected_system_info_->vpd_info->sku_number = base::nullopt;
  ExpectFetchSystemInfo();

  // Sku number file doesn't exist.
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();

  // Sku number file doesn't exist but should have.
  SetHasSkuNumber(true);
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

TEST_MISSING_FIELD(vpd_info, activate_date);
TEST_MISSING_FIELD(vpd_info, region);
TEST_MISSING_FIELD(vpd_info, mfg_date);
TEST_MISSING_FIELD(vpd_info, serial_number);
TEST_MISSING_FIELD(vpd_info, model_name);

TEST_F(SystemUtilsTest, TestNoSysDevicesVirtualDmiId) {
  expected_system_info_->dmi_info = nullptr;
  // Delete the whole directory |kRelativePathDmiInfo|.
  UnsetPath(kRelativePathDmiInfo);
  ExpectFetchSystemInfo();
}

TEST_MISSING_FIELD(dmi_info, bios_vendor);
TEST_MISSING_FIELD(dmi_info, bios_version);
TEST_MISSING_FIELD(dmi_info, board_name);
TEST_MISSING_FIELD(dmi_info, board_vendor);
TEST_MISSING_FIELD(dmi_info, board_version);
TEST_MISSING_FIELD(dmi_info, chassis_vendor);
TEST_MISSING_FIELD(dmi_info, product_family);
TEST_MISSING_FIELD(dmi_info, product_name);
TEST_MISSING_FIELD(dmi_info, product_version);
TEST_MISSING_FIELD(dmi_info, sys_vendor);

TEST_F(SystemUtilsTest, TestNoChassisType) {
  expected_system_info_->dmi_info->chassis_type = nullptr;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

TEST_F(SystemUtilsTest, TestBadChassisType) {
  // Overwrite the contents of |kChassisTypeFileName| with a chassis_type value
  // that cannot be parsed into an unsigned integer.
  std::string bad_chassis_type = "bad chassis type";
  SetMockFile({kRelativePathDmiInfo, kFileNameChassisType}, bad_chassis_type);
  ExpectFetchProbeError(mojo_ipc::ErrorType::kParseError);
}

TEST_F(SystemUtilsTest, TestNoOsVersion) {
  PopulateLsbRelease("");
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

TEST_F(SystemUtilsTest, TestBadOsVersion) {
  PopulateLsbRelease(
      "Milestone\n"
      "CHROMEOS_RELEASE_BUILD_NUMBER=1\n"
      "CHROMEOS_RELEASE_PATCH_NUMBER=2\n"
      "CHROMEOS_RELEASE_TRACK=3\n");
  ExpectFetchProbeError(mojo_ipc::ErrorType::kFileReadError);
}

TEST_F(SystemUtilsTest, TestBootMode) {
  expected_system_info_->os_info->boot_mode = mojo_ipc::BootMode::kCrosSecure;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();

  expected_system_info_->os_info->boot_mode = mojo_ipc::BootMode::kCrosEfi;
  SetUEFISceureBootResponse("\x00");
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();

  expected_system_info_->os_info->boot_mode = mojo_ipc::BootMode::kCrosLegacy;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();

  expected_system_info_->os_info->boot_mode = mojo_ipc::BootMode::kUnknown;
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();

  expected_system_info_->os_info->boot_mode =
      mojo_ipc::BootMode::kCrosEfiSecure;
  SetUEFISceureBootResponse("\x01");
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

// Test that the executor fails to read UEFISecureBoot file content and returns
// kCrosEfi as default value
TEST_F(SystemUtilsTest, TestUEFISceureBootFailure) {
  expected_system_info_->os_info->boot_mode = mojo_ipc::BootMode::kCrosEfi;
  EXPECT_CALL(*mock_executor(), GetUEFISecureBootContent(_))
      .Times(2)
      .WillRepeatedly(WithArg<0>(
          Invoke([](executor_ipc::Executor::GetUEFISecureBootContentCallback
                        callback) { std::move(callback).Run(""); })));
  SetSystemInfo(expected_system_info_);
  ExpectFetchSystemInfo();
}

}  // namespace
}  // namespace diagnostics

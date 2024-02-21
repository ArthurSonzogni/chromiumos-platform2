// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/fake_system_config.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/mojo_type_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::SizeIs;
using ::testing::WithArg;

MATCHER(IsNullMojoPtr, "") {
  return arg.is_null();
}

MATCHER(NotNullMojoPtr, "") {
  return !arg.is_null();
}

template <typename T>
std::optional<std::string> GetMockValue(const T& value) {
  return value;
}

std::optional<std::string> GetMockValue(const mojom::NullableUint64Ptr& ptr) {
  if (ptr)
    return base::NumberToString(ptr->value);
  return std::nullopt;
}

class SystemFetcherTest : public BaseFileTest {
 public:
  SystemFetcherTest(const SystemFetcherTest&) = delete;
  SystemFetcherTest& operator=(const SystemFetcherTest&) = delete;

 protected:
  SystemFetcherTest() = default;

  void SetUp() override {
    expected_system_info_ = mojom::SystemInfo::New();
    auto& vpd_info = expected_system_info_->vpd_info;
    vpd_info = mojom::VpdInfo::New();
    vpd_info->activate_date = "2020-40";
    vpd_info->mfg_date = "2019-01-01";
    vpd_info->model_name = "XX ModelName 007 XY";
    vpd_info->region = "us";
    vpd_info->serial_number = "8607G03EDF";
    vpd_info->sku_number = "ABCD&^A";
    vpd_info->oem_name = "FooOEM-VPD";
    auto& dmi_info = expected_system_info_->dmi_info;
    dmi_info = mojom::DmiInfo::New();
    dmi_info->bios_vendor = "Google";
    dmi_info->bios_version = "Google_BoardName.12200.68.0";
    dmi_info->board_name = "BoardName";
    dmi_info->board_vendor = "Google";
    dmi_info->board_version = "rev1234";
    dmi_info->chassis_vendor = "Google";
    dmi_info->chassis_type = mojom::NullableUint64::New(9);
    dmi_info->product_family = "FooFamily";
    dmi_info->product_name = "BarProductName";
    dmi_info->product_version = "rev1234";
    dmi_info->sys_vendor = "Google";
    auto& os_info = expected_system_info_->os_info;
    os_info = mojom::OsInfo::New();
    os_info->code_name = "CodeName";
    os_info->marketing_name = "Latitude 1234 Chromebook Enterprise";
    os_info->oem_name = "FooOEM";
    os_info->boot_mode = mojom::BootMode::kCrosSecure;
    os_info->efi_platform_size = mojom::OsInfo::EfiPlatformSize::kUnknown;
    auto& os_version = os_info->os_version;
    os_version = mojom::OsVersion::New();
    os_version->release_milestone = "87";
    os_version->build_number = "13544";
    os_version->branch_number = "59";
    os_version->patch_number = "0";
    os_version->release_channel = "stable-channel";
    SetSystemInfo(expected_system_info_);
    SetHasSkuNumber(true);

    // Default response for PSR info.
    ON_CALL(*mock_executor(), GetPsr(_))
        .WillByDefault([](mojom::Executor::GetPsrCallback callback) {
          std::move(callback).Run(
              mojom::GetPsrResult::NewError("Default error"));
        });
  }

  void SetSystemInfo(const mojom::SystemInfoPtr& system_info) {
    SetVpdInfo(system_info->vpd_info);
    SetDmiInfo(system_info->dmi_info);
    SetOsInfo(system_info->os_info);
  }

  void SetVpdInfo(const mojom::VpdInfoPtr& vpd_info) {
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
    SetMockFile({ro, kFileNameOemName}, vpd_info->oem_name);
  }

  void SetDmiInfo(const mojom::DmiInfoPtr& dmi_info) {
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

  void SetOsInfo(const mojom::OsInfoPtr& os_info) {
    ASSERT_FALSE(os_info.is_null());
    mock_context_.fake_system_config()->SetMarketingName(
        os_info->marketing_name);
    mock_context_.fake_system_config()->SetOemName(os_info->oem_name);
    mock_context_.fake_system_config()->SetCodeName(os_info->code_name);
    SetOsVersion(os_info->os_version);
    SetBootModeInProcCmd(os_info->boot_mode);
  }

  void SetOsVersion(const mojom::OsVersionPtr& os_version) {
    PopulateLsbRelease(base::StringPrintf(
        "CHROMEOS_RELEASE_CHROME_MILESTONE=%s\n"
        "CHROMEOS_RELEASE_BUILD_NUMBER=%s\n"
        "CHROMEOS_RELEASE_BRANCH_NUMBER=%s\n"
        "CHROMEOS_RELEASE_PATCH_NUMBER=%s\n"
        "CHROMEOS_RELEASE_TRACK=%s\n",
        os_version->release_milestone.c_str(), os_version->build_number.c_str(),
        os_version->branch_number.value().c_str(),
        os_version->patch_number.c_str(), os_version->release_channel.c_str()));
  }

  void SetBootModeInProcCmd(const mojom::BootMode& boot_mode) {
    std::optional<std::string> proc_cmd;
    switch (boot_mode) {
      case mojom::BootMode::kCrosSecure:
        proc_cmd = "Foo Bar=1 cros_secure Foo Bar=1";
        break;
      case mojom::BootMode::kCrosEfi:
        proc_cmd = "Foo Bar=1 cros_efi Foo Bar=1";
        break;
      case mojom::BootMode::kCrosEfiSecure:
        proc_cmd = "Foo Bar=1 cros_efi Foo Bar=1";
        break;
      case mojom::BootMode::kCrosLegacy:
        proc_cmd = "Foo Bar=1 cros_legacy Foo Bar=1";
        break;
      case mojom::BootMode::kUnknown:
        proc_cmd = "Foo Bar=1 Foo Bar=1";
        break;
    }
    SetMockFile(kFilePathProcCmdline, proc_cmd);
  }

  void SetMockExecutorReadFile(mojom::Executor::File file_enum,
                               const std::string& content) {
    // Set the mock executor response.
    EXPECT_CALL(*mock_executor(), ReadFile(file_enum, _))
        .WillOnce(base::test::RunOnceCallback<1>(content));
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

  // Fetches system info and saves it to |system_info|.
  // Generates assertion failure if the result is a probe error.
  void SaveSystemInfo(mojom::SystemInfoPtr& system_info) {
    auto system_result = FetchSystemInfoSync();
    ASSERT_FALSE(system_result.is_null());
    ASSERT_TRUE(system_result->is_system_info());
    system_info = std::move(system_result->get_system_info());
  }

  void ExpectFetchSystemInfo() {
    auto system_result = FetchSystemInfoSync();
    ASSERT_FALSE(system_result.is_null());
    ASSERT_FALSE(system_result->is_error());
    ASSERT_TRUE(system_result->is_system_info());
    auto res = std::move(system_result->get_system_info());
    EXPECT_EQ(res, expected_system_info_)
        << GetDiffString(res, expected_system_info_);
  }

  void ExpectFetchProbeError(const mojom::ErrorType& expected) {
    auto system_result = FetchSystemInfoSync();
    ASSERT_TRUE(system_result->is_error());
    EXPECT_EQ(system_result->get_error()->type, expected);
  }

 protected:
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  FakeSystemConfig* fake_system_config() {
    return mock_context_.fake_system_config();
  }

  mojom::SystemResultPtr FetchSystemInfoSync() {
    base::test::TestFuture<mojom::SystemResultPtr> future;
    FetchSystemInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  mojom::SystemInfoPtr expected_system_info_;

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  std::unique_ptr<base::test::ScopedChromeOSVersionInfo> chromeos_version_;
};

TEST_F(SystemFetcherTest, FetchSystemInfo) {
  ExpectFetchSystemInfo();
}

TEST_F(SystemFetcherTest, NoVpdDirSkuNumberRequired) {
  UnsetPath(base::FilePath(kRelativePathVpdRo).DirName());
  SetHasSkuNumber(true);
  ExpectFetchProbeError(mojom::ErrorType::kFileReadError);
}

TEST_F(SystemFetcherTest, NoVpdDirSkuNumberNotRequired) {
  UnsetPath(base::FilePath(kRelativePathVpdRo).DirName());
  SetHasSkuNumber(false);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  EXPECT_THAT(system_info->vpd_info, IsNullMojoPtr());
}

// Test if the fallback logic triggered by missing OEM name in cros-config works
// when there's no VPD.
TEST_F(SystemFetcherTest, NoVpdDirAndNoOemNameInCrosconfig) {
  UnsetPath(base::FilePath(kRelativePathVpdRo).DirName());
  fake_system_config()->SetOemName(std::nullopt);
  SetHasSkuNumber(false);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->oem_name, std::nullopt);
}

TEST_F(SystemFetcherTest, SkuNumberExistsButNotRequired) {
  SetHasSkuNumber(false);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->sku_number, std::nullopt);
}

TEST_F(SystemFetcherTest, NoSkuNumberWhenItIsNotRequired) {
  UnsetPath({kRelativePathVpdRo, kFileNameSkuNumber});
  SetHasSkuNumber(false);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->sku_number, std::nullopt);
}

TEST_F(SystemFetcherTest, NoSkuNumberWhenItIsRequired) {
  UnsetPath({kRelativePathVpdRo, kFileNameSkuNumber});
  SetHasSkuNumber(true);
  ExpectFetchProbeError(mojom::ErrorType::kFileReadError);
}

TEST_F(SystemFetcherTest, NoVpdActivateDate) {
  UnsetPath({kRelativePathVpdRw, kFileNameActivateDate});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->activate_date, std::nullopt);
}

TEST_F(SystemFetcherTest, NoVpdRegion) {
  UnsetPath({kRelativePathVpdRo, kFileNameRegion});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->region, std::nullopt);
}

TEST_F(SystemFetcherTest, NoVpdMfgDate) {
  UnsetPath({kRelativePathVpdRo, kFileNameMfgDate});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->mfg_date, std::nullopt);
}

TEST_F(SystemFetcherTest, NoVpdSerialNumber) {
  UnsetPath({kRelativePathVpdRo, kFileNameSerialNumber});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->serial_number, std::nullopt);
}

TEST_F(SystemFetcherTest, NoVpdModelName) {
  UnsetPath({kRelativePathVpdRo, kFileNameModelName});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->model_name, std::nullopt);
}

TEST_F(SystemFetcherTest, NoVpdOemName) {
  UnsetPath({kRelativePathVpdRo, kFileNameOemName});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->oem_name, std::nullopt);
}

TEST_F(SystemFetcherTest, NoSysDevicesVirtualDmiId) {
  UnsetPath(kRelativePathDmiInfo);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  EXPECT_THAT(system_info->dmi_info, IsNullMojoPtr());
}

TEST_F(SystemFetcherTest, NoDmiBiosVendor) {
  UnsetPath({kRelativePathDmiInfo, kFileNameBiosVendor});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->bios_vendor, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiBiosVersion) {
  UnsetPath({kRelativePathDmiInfo, kFileNameBiosVersion});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->bios_version, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiBoardName) {
  UnsetPath({kRelativePathDmiInfo, kFileNameBoardName});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->board_name, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiBoardVendor) {
  UnsetPath({kRelativePathDmiInfo, kFileNameBoardVendor});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->board_vendor, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiBoardVersion) {
  UnsetPath({kRelativePathDmiInfo, kFileNameBoardVersion});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->board_version, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiChassisVendor) {
  UnsetPath({kRelativePathDmiInfo, kFileNameChassisVendor});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->chassis_vendor, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiChassisType) {
  UnsetPath({kRelativePathDmiInfo, kFileNameChassisType});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_THAT(system_info->dmi_info->chassis_type, IsNullMojoPtr());
}

TEST_F(SystemFetcherTest, NoDmiProductFamily) {
  UnsetPath({kRelativePathDmiInfo, kFileNameProductFamily});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->product_family, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiProductName) {
  UnsetPath({kRelativePathDmiInfo, kFileNameProductName});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->product_name, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiProductVersion) {
  UnsetPath({kRelativePathDmiInfo, kFileNameProductVersion});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->product_version, std::nullopt);
}

TEST_F(SystemFetcherTest, NoDmiSysVendor) {
  UnsetPath({kRelativePathDmiInfo, kFileNameSysVendor});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->dmi_info);
  EXPECT_EQ(system_info->dmi_info->sys_vendor, std::nullopt);
}

TEST_F(SystemFetcherTest, BadChassisType) {
  SetMockFile({kRelativePathDmiInfo, kFileNameChassisType}, "bad chassis type");
  ExpectFetchProbeError(mojom::ErrorType::kParseError);
}

TEST_F(SystemFetcherTest, NoOsVersion) {
  PopulateLsbRelease("");
  ExpectFetchProbeError(mojom::ErrorType::kFileReadError);
}

TEST_F(SystemFetcherTest, BadOsVersion) {
  PopulateLsbRelease(
      "Milestone\n"
      "CHROMEOS_RELEASE_BUILD_NUMBER=1\n"
      "CHROMEOS_RELEASE_PATCH_NUMBER=2\n"
      "CHROMEOS_RELEASE_TRACK=3\n");
  ExpectFetchProbeError(mojom::ErrorType::kFileReadError);
}

TEST_F(SystemFetcherTest, BootModeCrosSecure) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosSecure);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kCrosSecure);
}

TEST_F(SystemFetcherTest, BootModeCrosEfi) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfi);
  // Use string constructor to prevent string truncation from null bytes.
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable,
                          std::string("\x00\x00\x00\x00\x00", 5));
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kCrosEfi);
}

TEST_F(SystemFetcherTest, BootModeCrosLegacy) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosLegacy);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kCrosLegacy);
}

TEST_F(SystemFetcherTest, BootModeUnknown) {
  SetBootModeInProcCmd(mojom::BootMode::kUnknown);

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kUnknown);
}

TEST_F(SystemFetcherTest, BootModeCrosEfiSecure) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfiSecure);
  // Use string constructor to prevent string truncation from null bytes.
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable,
                          std::string("\x00\x00\x00\x00\x01", 5));
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kCrosEfiSecure);
}

TEST_F(SystemFetcherTest, BootModeDefaultToCrosEfi) {
  // Test that the executor fails to read UEFISecureBoot file content and
  // returns kCrosEfi as default value.
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfi);
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable, "");
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->boot_mode, mojom::BootMode::kCrosEfi);
}

TEST_F(SystemFetcherTest, EfiPlatformSizeUnknown) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfi);
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "");
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable,
                          std::string("\x00\x00\x00\x00\x00", 5));

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->efi_platform_size,
            mojom::OsInfo::EfiPlatformSize::kUnknown);
}

TEST_F(SystemFetcherTest, EfiPlatformSize64) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfi);
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "64");
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable,
                          std::string("\x00\x00\x00\x00\x00", 5));

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->efi_platform_size,
            mojom::OsInfo::EfiPlatformSize::k64);
}

TEST_F(SystemFetcherTest, EfiPlatformSize32) {
  SetBootModeInProcCmd(mojom::BootMode::kCrosEfi);
  SetMockExecutorReadFile(mojom::Executor::File::kUEFIPlatformSize, "32");
  SetMockExecutorReadFile(mojom::Executor::File::kUEFISecureBootVariable,
                          std::string("\x00\x00\x00\x00\x00", 5));

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->efi_platform_size,
            mojom::OsInfo::EfiPlatformSize::k32);
}

TEST_F(SystemFetcherTest, OemName) {
  fake_system_config()->SetOemName("FooOEM");
  SetMockFile({kRelativePathVpdRo, kFileNameOemName}, "FooOEM-VPD");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->oem_name, "FooOEM");
  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->oem_name, "FooOEM-VPD");
}

TEST_F(SystemFetcherTest, OemNameVpdEmpty) {
  fake_system_config()->SetOemName("FooOEM");
  SetMockFile({kRelativePathVpdRo, kFileNameOemName}, "");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->oem_name, "FooOEM");
  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->oem_name, "");
}

TEST_F(SystemFetcherTest, OemNameMissing) {
  fake_system_config()->SetOemName(std::nullopt);
  UnsetPath({kRelativePathVpdRo, kFileNameOemName});

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->oem_name, std::nullopt);
  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->oem_name, std::nullopt);
}

TEST_F(SystemFetcherTest, OemNameOsFallbackToVpd) {
  // Test the fallback logic triggered by missing OEM name in cros-config.
  fake_system_config()->SetOemName(std::nullopt);
  SetMockFile({kRelativePathVpdRo, kFileNameOemName}, "FooOEM-VPD");

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  ASSERT_TRUE(system_info->os_info);
  EXPECT_EQ(system_info->os_info->oem_name, "FooOEM-VPD");
  ASSERT_TRUE(system_info->vpd_info);
  EXPECT_EQ(system_info->vpd_info->oem_name, "FooOEM-VPD");
}

TEST_F(SystemFetcherTest, PsrInfo) {
  auto result = mojom::PsrInfo::New();
  result->log_state = mojom::PsrInfo::LogState::kStarted;
  result->uuid = "ed6703fa-d312-4e8b-9ddd-2155bb2dee65";
  result->upid = "ok6703fa-d312-4e8b-9ddd-2155bb2dee65";
  result->log_start_date = 163987200;
  result->oem_name = "Panasonic";
  result->oem_make = "Toughbook";
  result->oem_model = "55";
  result->manufacture_country = "United States";
  result->oem_data = "None";
  result->uptime_seconds = 30233443;
  result->s5_counter = 3;
  result->s4_counter = 2;
  result->s3_counter = 1;
  result->warm_reset_counter = 0;
  auto event = mojom::PsrEvent::New();
  event->type = mojom::PsrEvent::EventType::kLogStart;
  event->time = 163987200;
  event->data = 342897977;
  result->events.push_back(event.Clone());
  event->type = mojom::PsrEvent::EventType::kPrtcFailure;
  event->time = 453987200;
  event->data = 643897977;
  result->events.push_back(event.Clone());
  result->is_supported = true;

  EXPECT_CALL(*mock_executor(), GetPsr(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::GetPsrResult::NewInfo(std::move(result))));

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  auto& psr_info = system_info->psr_info;
  EXPECT_EQ(psr_info->log_state, mojom::PsrInfo::LogState::kStarted);
  EXPECT_EQ(psr_info->uuid, "ed6703fa-d312-4e8b-9ddd-2155bb2dee65");
  EXPECT_EQ(psr_info->upid, "ok6703fa-d312-4e8b-9ddd-2155bb2dee65");
  EXPECT_EQ(psr_info->log_start_date, 163987200);
  EXPECT_EQ(psr_info->oem_name, "Panasonic");
  EXPECT_EQ(psr_info->oem_make, "Toughbook");
  EXPECT_EQ(psr_info->oem_model, "55");
  EXPECT_EQ(psr_info->manufacture_country, "United States");
  EXPECT_EQ(psr_info->oem_data, "None");
  EXPECT_EQ(psr_info->uptime_seconds, 30233443);
  EXPECT_EQ(psr_info->s5_counter, 3);
  EXPECT_EQ(psr_info->s4_counter, 2);
  EXPECT_EQ(psr_info->s3_counter, 1);
  EXPECT_EQ(psr_info->warm_reset_counter, 0);
  EXPECT_EQ(psr_info->is_supported, true);

  ASSERT_THAT(psr_info->events, SizeIs(2));
  auto& event0 = psr_info->events[0];
  ASSERT_THAT(event0, NotNullMojoPtr());
  EXPECT_EQ(event0->type, mojom::PsrEvent::EventType::kLogStart);
  EXPECT_EQ(event0->time, 163987200);
  EXPECT_EQ(event0->data, 342897977);
  auto& event1 = psr_info->events[1];
  ASSERT_THAT(event1, NotNullMojoPtr());
  EXPECT_EQ(event1->type, mojom::PsrEvent::EventType::kPrtcFailure);
  EXPECT_EQ(event1->time, 453987200);
  EXPECT_EQ(event1->data, 643897977);
}

TEST_F(SystemFetcherTest, PsrError) {
  EXPECT_CALL(*mock_executor(), GetPsr(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          mojom::GetPsrResult::NewError("GetPsr error")));

  mojom::SystemInfoPtr system_info;
  ASSERT_NO_FATAL_FAILURE(SaveSystemInfo(system_info));

  EXPECT_THAT(system_info->psr_info, IsNullMojoPtr());
}

}  // namespace
}  // namespace diagnostics

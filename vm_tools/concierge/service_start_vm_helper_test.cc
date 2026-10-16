// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_start_vm_helper.h"

#include <fcntl.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <brillo/files/safe_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/vm_builder.h"

namespace vm_tools {
namespace concierge {

TEST(ServiceStartVMHelperTest, TestCheckCpuCount) {
  StartVmRequest fake_request;
  StartVmResponse fake_response;
  int max_cpu = base::SysInfo::NumberOfProcessors();

  // Test valid cpu count
  fake_request.set_cpus(max_cpu);
  EXPECT_EQ(CheckCpuCount(fake_request, &fake_response), true);

  // Test invalid cpu count
  fake_request.set_cpus(max_cpu + 1);
  EXPECT_EQ(CheckCpuCount(fake_request, &fake_response), false);
}

TEST(StartVMHelperTest, TestClassifyVmVariants) {
  StartVmRequest fake_request_vm_type;

  // Classify vm by request's vm_type
  fake_request_vm_type.set_vm_type(VmInfo::BOREALIS);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type), apps::VmType::BOREALIS);
  fake_request_vm_type.set_vm_type(VmInfo::TERMINA);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type), apps::VmType::TERMINA);
  fake_request_vm_type.set_vm_type(VmInfo::BRUSCHETTA);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type),
            apps::VmType::BRUSCHETTA);
  fake_request_vm_type.set_vm_type(VmInfo::UNKNOWN);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type), apps::VmType::UNKNOWN);

  // Classify vm by dlc_id with UNKNOWN vm_type
  fake_request_vm_type.mutable_vm()->set_dlc_id(kBorealisBiosDlcId);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type), apps::VmType::BOREALIS);
  fake_request_vm_type.mutable_vm()->set_dlc_id(kBruschettaBiosDlcId);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type),
            apps::VmType::BRUSCHETTA);
  fake_request_vm_type.clear_vm();

  // start_termina boolean function specify TERMINA VM
  fake_request_vm_type.set_start_termina(true);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type), apps::VmType::TERMINA);
  fake_request_vm_type.set_start_termina(false);
}

TEST(StartVMHelperTest, TestGetVmCpuArgs) {
  int cpu_nums = 8;
  // Create temporary test directories for CPU information
  // The file contents are taken from real DUT
  base::ScopedTempDir cpu_info_dir;
  EXPECT_TRUE(cpu_info_dir.CreateUniqueTempDir());
  for (int i = 0; i < cpu_nums; i++) {
    EXPECT_TRUE(base::CreateDirectory(cpu_info_dir.GetPath().Append(
        base::StringPrintf("cpu%d/topology/", i))));
  }

  // Create package_id file and write id
  for (int i = 0; i < cpu_nums; i++) {
    base::FilePath cpu_id_path = cpu_info_dir.GetPath().Append(
        base::StringPrintf("cpu%d/topology/physical_package_id", i));

    std::string test_cpu_id =
        i < cpu_nums / 2 ? std::to_string(0) : std::to_string(1);
    EXPECT_TRUE(base::WriteFile(cpu_id_path, test_cpu_id));
  }

  // Create package_id file and write id
  for (int i = 0; i < cpu_nums; i++) {
    base::FilePath cpu_capacity_path = cpu_info_dir.GetPath().Append(
        base::StringPrintf("cpu%d/cpu_capacity", i));
    std::string test_cpu_capacity =
        i < cpu_nums / 2 ? std::to_string(741) : std::to_string(1024);
    EXPECT_TRUE(base::WriteFile(cpu_capacity_path, test_cpu_capacity));
  }

  // Run GetVmCpuArgs on test files
  VmBuilder::VmCpuArgs vm_cpu_args =
      internal::GetVmCpuArgs(8, cpu_info_dir.GetPath());

  EXPECT_EQ(vm_cpu_args.cpu_affinity,
            "0=0,1,2,3:1=0,1,2,3:2=0,1,2,3:3=0,1,2,3:4=4,5,6,7:5=4,5,6,7:6=4,5,"
            "6,7:7=4,5,6,7");
  std::vector<std::string> cpu_capacity_vec = {"0=741",  "1=741",  "2=741",
                                               "3=741",  "4=1024", "5=1024",
                                               "6=1024", "7=1024"};
  EXPECT_EQ(vm_cpu_args.cpu_capacity, cpu_capacity_vec);
  std::vector<std::vector<std::string>> cpu_cluster_vec = {
      {{"0"}, {"1"}, {"2"}, {"3"}}, {{"4"}, {"5"}, {"6"}, {"7"}}};
  EXPECT_EQ(vm_cpu_args.cpu_clusters, cpu_cluster_vec);
}

TEST(StartVMHelperTest, TestAugmentImageSpec) {
  auto root_fd = brillo::SafeFD::Root();
  ASSERT_FALSE(brillo::SafeFD::IsError(root_fd.second));

  base::ScopedFD kernel_fd(open("/dev/null", O_RDWR | O_CLOEXEC));
  base::ScopedFD rootfs_fd(open("/dev/null", O_RDWR | O_CLOEXEC));
  base::ScopedFD initrd_fd(open("/dev/null", O_RDWR | O_CLOEXEC));
  base::ScopedFD bios_fd(open("/dev/null", O_RDWR | O_CLOEXEC));
  base::ScopedFD pflash_fd(open("/dev/null", O_RDWR | O_CLOEXEC));

  int kernel_raw_fd = kernel_fd.get();
  int rootfs_raw_fd = rootfs_fd.get();
  int initrd_raw_fd = initrd_fd.get();
  int bios_raw_fd = bios_fd.get();
  int pflash_raw_fd = pflash_fd.get();

  EXPECT_EQ(fcntl(kernel_raw_fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);

  VMImageSpec image_spec{
      .kernel = std::move(kernel_fd),
      .initrd = std::move(initrd_fd),
      .rootfs = std::move(rootfs_fd),
      .bios = std::move(bios_fd),
      .pflash = std::move(pflash_fd),
  };

  // Augment a VM image spec with user defined kernel, rootfs, and initrd.
  auto augmented_spec =
      internal::AugmentImageSpec(root_fd.first, false /* is_rootfs_writable */,
                                 {}, {}, {}, std::move(image_spec));
  ASSERT_TRUE(augmented_spec.has_value());
  EXPECT_TRUE(augmented_spec->kernel.is_valid());
  EXPECT_EQ(augmented_spec->kernel.get(), kernel_raw_fd);
  EXPECT_EQ(fcntl(augmented_spec->kernel.get(), F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);

  EXPECT_TRUE(augmented_spec->rootfs.is_valid());
  EXPECT_EQ(augmented_spec->rootfs.get(), rootfs_raw_fd);
  EXPECT_EQ(fcntl(augmented_spec->rootfs.get(), F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);

  EXPECT_TRUE(augmented_spec->initrd.is_valid());
  EXPECT_EQ(augmented_spec->initrd.get(), initrd_raw_fd);
  EXPECT_EQ(fcntl(augmented_spec->initrd.get(), F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);

  EXPECT_TRUE(augmented_spec->bios.is_valid());
  EXPECT_EQ(augmented_spec->bios.get(), bios_raw_fd);
  EXPECT_EQ(fcntl(augmented_spec->bios.get(), F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);

  EXPECT_TRUE(augmented_spec->pflash.is_valid());
  EXPECT_EQ(augmented_spec->pflash.get(), pflash_raw_fd);
  EXPECT_EQ(fcntl(augmented_spec->pflash.get(), F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);

  // Test failure when neither kernel nor bios is provided
  VMImageSpec empty_spec;
  auto fail_res = internal::AugmentImageSpec(root_fd.first, false, {}, {}, {},
                                             std::move(empty_spec));
  EXPECT_FALSE(fail_res.has_value());
  EXPECT_FALSE(fail_res.error().empty());

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  ASSERT_TRUE(
      base::CreateDirectory(temp_dir.GetPath().Append("bios").Append("opt")));
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("vm")));
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("tools")));

  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("bios").Append(kBruschettaBiosDlcPath), ""));
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("vm").Append(kVmKernelName), ""));
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("vm").Append(kVmRootfsName), ""));
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("vm").Append(kVmToolsDiskName), ""));
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("tools").Append(kVmKernelName), ""));
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append("tools").Append(kVmToolsDiskName), ""));

  brillo::SafeFD::SetRootPathForTesting(temp_dir.GetPath().value().c_str());
  base::ScopedClosureRunner reset_root(
      base::BindOnce(&brillo::SafeFD::SetRootPathForTesting, "/"));

  auto test_root_fd = brillo::SafeFD::Root();
  ASSERT_FALSE(brillo::SafeFD::IsError(test_root_fd.second));

  base::FilePath bios_dlc_path("bios");
  base::FilePath vm_dlc_path("vm");
  base::FilePath tools_dlc_path("tools");

  // 1. bios_dlc_path + tools_dlc_path
  {
    auto res = internal::AugmentImageSpec(
        test_root_fd.first, /*is_rootfs_writable=*/false, bios_dlc_path,
        /*vm_dlc_path=*/std::nullopt, tools_dlc_path, VMImageSpec{});
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->kernel.is_valid());
    EXPECT_FALSE(res->rootfs.is_valid());
    EXPECT_FALSE(res->initrd.is_valid());
    EXPECT_TRUE(res->bios.is_valid());
    EXPECT_EQ(fcntl(res->bios.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_TRUE(res->tools_disk.is_valid());
    EXPECT_EQ(fcntl(res->tools_disk.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  }

  // 2. User bios FD + tools_dlc_path
  {
    base::ScopedFD user_bios_fd(open("/dev/null", O_RDWR | O_CLOEXEC));
    ASSERT_TRUE(user_bios_fd.is_valid());
    int user_bios_raw = user_bios_fd.get();
    EXPECT_EQ(fcntl(user_bios_raw, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    VMImageSpec user_spec{.bios = std::move(user_bios_fd)};

    auto res = internal::AugmentImageSpec(
        test_root_fd.first, /*is_rootfs_writable=*/false,
        /*bios_dlc_path=*/std::nullopt, /*vm_dlc_path=*/std::nullopt,
        tools_dlc_path, std::move(user_spec));
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->kernel.is_valid());
    EXPECT_FALSE(res->rootfs.is_valid());
    EXPECT_FALSE(res->initrd.is_valid());
    EXPECT_TRUE(res->bios.is_valid());
    EXPECT_EQ(res->bios.get(), user_bios_raw);
    EXPECT_EQ(fcntl(res->bios.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_TRUE(res->tools_disk.is_valid());
    EXPECT_EQ(fcntl(res->tools_disk.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  }

  // 3. tools_dlc_path alone without BIOS (fallback kernel)
  {
    auto res = internal::AugmentImageSpec(
        test_root_fd.first, /*is_rootfs_writable=*/false,
        /*bios_dlc_path=*/std::nullopt, /*vm_dlc_path=*/std::nullopt,
        tools_dlc_path, VMImageSpec{});
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->kernel.is_valid());
    EXPECT_EQ(fcntl(res->kernel.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_FALSE(res->rootfs.is_valid());
    EXPECT_FALSE(res->initrd.is_valid());
    EXPECT_FALSE(res->bios.is_valid());
    EXPECT_TRUE(res->tools_disk.is_valid());
    EXPECT_EQ(fcntl(res->tools_disk.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  }

  // 4. bios_dlc_path + vm_dlc_path
  {
    auto res = internal::AugmentImageSpec(
        test_root_fd.first, /*is_rootfs_writable=*/false, bios_dlc_path,
        vm_dlc_path, /*tools_dlc_path=*/std::nullopt, VMImageSpec{});
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->kernel.is_valid());
    EXPECT_EQ(fcntl(res->kernel.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_TRUE(res->rootfs.is_valid());
    EXPECT_EQ(fcntl(res->rootfs.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_FALSE(res->initrd.is_valid());
    EXPECT_TRUE(res->bios.is_valid());
    EXPECT_EQ(fcntl(res->bios.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
    EXPECT_TRUE(res->tools_disk.is_valid());
    EXPECT_EQ(fcntl(res->tools_disk.get(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  }
}

}  // namespace concierge
}  // namespace vm_tools

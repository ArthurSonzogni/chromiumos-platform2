// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_start_vm_helper.h"

#include <string>
#include <vector>
#include "vm_tools/concierge/vm_builder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vm_concierge/concierge_service.pb.h>

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

  // VM has bios FD can be specified as BRUSCHETTA
  fake_request_vm_type.add_fds(StartVmRequest::BIOS);
  EXPECT_EQ(internal::ClassifyVm(fake_request_vm_type),
            apps::VmType::BRUSCHETTA);
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
    int ret =
        base::WriteFile(cpu_id_path, test_cpu_id.c_str(), test_cpu_id.length());
    EXPECT_EQ(ret, test_cpu_id.length());
  }

  // Create package_id file and write id
  for (int i = 0; i < cpu_nums; i++) {
    base::FilePath cpu_capacity_path = cpu_info_dir.GetPath().Append(
        base::StringPrintf("cpu%d/cpu_capacity", i));
    std::string test_cpu_capacity =
        i < cpu_nums / 2 ? std::to_string(741) : std::to_string(1024);
    int ret = base::WriteFile(cpu_capacity_path, test_cpu_capacity.c_str(),
                              test_cpu_capacity.length());
    EXPECT_EQ(ret, test_cpu_capacity.length());
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
}  // namespace concierge
}  // namespace vm_tools

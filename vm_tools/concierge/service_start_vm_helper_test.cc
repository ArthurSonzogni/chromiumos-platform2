// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_start_vm_helper.h"

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

}  // namespace concierge
}  // namespace vm_tools

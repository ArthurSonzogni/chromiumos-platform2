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

}  // namespace concierge
}  // namespace vm_tools

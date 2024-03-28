// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_cpu.h"

#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

#include "runtime_probe/utils/function_test_utils.h"
#include "runtime_probe/utils/mojo_test_utils.h"

namespace runtime_probe {

namespace {

constexpr char kFakeModelName[] = "fake model name";

}  // namespace

class GenericCpuTest : public BaseFunctionTest {
 public:
  void SetUp() override {
    mock_context()->SetCrosHealthdProbeService(&fake_service_);
  }

 protected:
  FakeCrosHealthdProbeService fake_service_;
};

TEST_F(GenericCpuTest, Suceed) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_info = cros_healthd_mojom::CpuInfo::New();
  cpu_info->num_total_threads = 4;
  cpu_info->physical_cpus.push_back(cros_healthd_mojom::PhysicalCpuInfo::New());
  cpu_info->physical_cpus[0]->model_name = kFakeModelName;

  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": "4",
        "model": "fake model name"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(GenericCpuTest, BigAndSmallCores) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_info = cros_healthd_mojom::CpuInfo::New();
  cpu_info->num_total_threads = 4;
  cpu_info->physical_cpus.push_back(cros_healthd_mojom::PhysicalCpuInfo::New());
  cpu_info->physical_cpus.push_back(cros_healthd_mojom::PhysicalCpuInfo::New());
  cpu_info->physical_cpus.push_back(cros_healthd_mojom::PhysicalCpuInfo::New());
  cpu_info->physical_cpus[0]->model_name = kFakeModelName;
  cpu_info->physical_cpus[1]->model_name = "Big/Small core CPU";
  cpu_info->physical_cpus[2]->model_name = "Big/Small core CPU";

  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": "4",
        "model": "fake model name"
      },
      {
        "cores": "4",
        "model": "Big/Small core CPU"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(GenericCpuTest, NoModelName) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_info = cros_healthd_mojom::CpuInfo::New();
  cpu_info->physical_cpus.push_back(cros_healthd_mojom::PhysicalCpuInfo::New());
  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": "0",
        "model": "unknown"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(GenericCpuTest, ProbeError) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_result = cros_healthd_mojom::CpuResult::NewError(
      cros_healthd_mojom::ProbeError::New());
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

}  // namespace runtime_probe

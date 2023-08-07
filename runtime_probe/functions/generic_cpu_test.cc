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

namespace runtime_probe {

namespace {

constexpr char kFakeModelName[] = "fake model name";

// Create |PhysicalCpuInfo| with only core_ids.
cros_healthd_mojom::PhysicalCpuInfoPtr CreatePhysicalCpuInfo(
    const std::vector<uint32_t>& core_ids) {
  auto physical_cpu_info = cros_healthd_mojom::PhysicalCpuInfo::New();
  for (const auto& core_id : core_ids) {
    auto logical_cpu_info = cros_healthd_mojom::LogicalCpuInfo::New();
    logical_cpu_info->core_id = core_id;
    physical_cpu_info->logical_cpus.push_back(std::move(logical_cpu_info));
  }
  return physical_cpu_info;
}

// Create |PhysicalCpuInfo| with only model_name and core_ids.
cros_healthd_mojom::PhysicalCpuInfoPtr CreatePhysicalCpuInfo(
    const std::string& model_name, const std::vector<uint32_t>& core_ids) {
  auto physical_cpu_info = CreatePhysicalCpuInfo(core_ids);
  physical_cpu_info->model_name = model_name;
  return physical_cpu_info;
}

// Fake CrosHealthdProbeService for testing.
class FakeCrosHealthdProbeService
    : public cros_healthd_mojom::CrosHealthdProbeService {
 public:
  void ProbeTelemetryInfo(
      const std::vector<cros_healthd_mojom::ProbeCategoryEnum>&,
      ProbeTelemetryInfoCallback callback) override {
    std::move(callback).Run(std::move(telemetry_info_ptr_));
  }

  // Unused mocked function.
  void ProbeProcessInfo(uint32_t process_id,
                        ProbeProcessInfoCallback callback) override {
    NOTREACHED_NORETURN();
  };

  // Unused mocked function.
  void ProbeMultipleProcessInfo(
      const std::optional<std::vector<uint32_t>>& process_ids,
      bool ignore_single_process_error,
      ProbeMultipleProcessInfoCallback callback) override {
    NOTREACHED_NORETURN();
  };

  void SetCpuResult(cros_healthd_mojom::CpuResultPtr cpu_result) {
    telemetry_info_ptr_->cpu_result = std::move(cpu_result);
  }

 private:
  cros_healthd_mojom::TelemetryInfoPtr telemetry_info_ptr_{
      cros_healthd_mojom::TelemetryInfo::New()};
};

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
  cpu_info->physical_cpus.push_back(
      CreatePhysicalCpuInfo(kFakeModelName, {1, 2, 3, 4}));
  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": 4,
        "model": "fake model name"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(GenericCpuTest, SingleCoreMultiThreads) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_info = cros_healthd_mojom::CpuInfo::New();
  cpu_info->physical_cpus.push_back(
      CreatePhysicalCpuInfo(kFakeModelName, {1, 1, 2, 2}));
  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": 2,
        "model": "fake model name"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(GenericCpuTest, NoModelName) {
  auto probe_function = CreateProbeFunction<GenericCpuFunction>();
  auto cpu_info = cros_healthd_mojom::CpuInfo::New();
  cpu_info->physical_cpus.push_back(CreatePhysicalCpuInfo({1, 2, 3, 4}));
  auto cpu_result =
      cros_healthd_mojom::CpuResult::NewCpuInfo(std::move(cpu_info));
  fake_service_.SetCpuResult(std::move(cpu_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "cores": 4,
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

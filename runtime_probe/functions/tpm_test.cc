// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/tpm.h"

#include <utility>

#include <base/check.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

#include "runtime_probe/utils/function_test_utils.h"
#include "runtime_probe/utils/mojo_test_utils.h"

namespace runtime_probe {

class TpmTest : public BaseFunctionTest {
 public:
  void SetUp() override {
    mock_context()->SetCrosHealthdProbeService(&fake_service_);
  }

 protected:
  FakeCrosHealthdProbeService fake_service_;
};

TEST_F(TpmTest, Suceed) {
  auto probe_function = CreateProbeFunction<TpmFunction>();
  auto tpm_info = cros_healthd_mojom::TpmInfo::New();
  tpm_info->version = cros_healthd_mojom::TpmVersion::New();
  tpm_info->version->spec_level = 162;
  tpm_info->version->manufacturer = 0x43524f53;
  tpm_info->version->vendor_specific = "xCG fTPM";
  auto tpm_result =
      cros_healthd_mojom::TpmResult::NewTpmInfo(std::move(tpm_info));
  fake_service_.SetTpmResult(std::move(tpm_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "manufacturer": "0x43524f53",
        "spec_level": "162",
        "vendor_specific": "xCG fTPM"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(TpmTest, UnknownVendor) {
  auto probe_function = CreateProbeFunction<TpmFunction>();
  auto tpm_info = cros_healthd_mojom::TpmInfo::New();
  tpm_info->version = cros_healthd_mojom::TpmVersion::New();
  auto tpm_result =
      cros_healthd_mojom::TpmResult::NewTpmInfo(std::move(tpm_info));
  fake_service_.SetTpmResult(std::move(tpm_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      {
        "manufacturer": "0x0",
        "spec_level": "0",
        "vendor_specific": "unknown"
      }
    ]
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(TpmTest, Tpm2Simulator) {
  auto probe_function = CreateProbeFunction<TpmFunction>();
  auto tpm_info = cros_healthd_mojom::TpmInfo::New();
  tpm_info->version = cros_healthd_mojom::TpmVersion::New();
  tpm_info->version->manufacturer = 0x53494d55;
  auto tpm_result =
      cros_healthd_mojom::TpmResult::NewTpmInfo(std::move(tpm_info));
  fake_service_.SetTpmResult(std::move(tpm_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

TEST_F(TpmTest, ProbeError) {
  auto probe_function = CreateProbeFunction<TpmFunction>();
  auto tpm_result = cros_healthd_mojom::TpmResult::NewError(
      cros_healthd_mojom::ProbeError::New());
  fake_service_.SetTpmResult(std::move(tpm_result));

  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(EvalProbeFunction(probe_function.get()), ans);
}

}  // namespace runtime_probe

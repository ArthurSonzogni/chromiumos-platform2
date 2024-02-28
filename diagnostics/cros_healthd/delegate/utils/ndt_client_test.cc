// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/ndt_client.h"

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libndt7/libndt7.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace libndt7 = ::measurementlab::libndt7;

class FakeNdtClient final : public libndt7::Client {
 public:
  FakeNdtClient() = default;
  FakeNdtClient(const FakeNdtClient&) = delete;
  FakeNdtClient& operator=(const FakeNdtClient&) = delete;
  ~FakeNdtClient() override = default;

  // libndt7::Client overrides:
  bool run() override { return is_success_; }
  libndt7::SummaryData get_summary() override { return data_; };

  void SetTestResult(bool is_success) { is_success_ = is_success; }
  void SetDownloadSpeedResult(double speed) { data_.download_speed = speed; }
  void SetUploadSpeedResult(double speed) { data_.upload_speed = speed; }

 private:
  bool is_success_ = false;
  libndt7::SummaryData data_;
};

class NdtClientTest : public ::testing::Test {
 public:
  NdtClientTest(const NdtClientTest&) = delete;
  NdtClientTest& operator=(const NdtClientTest&) = delete;

 protected:
  NdtClientTest() = default;

  void SetUp() override { client_ = std::make_unique<FakeNdtClient>(); }

  std::unique_ptr<FakeNdtClient> client_;
};

TEST_F(NdtClientTest, DownloadTestPassed) {
  client_->SetTestResult(/*is_success=*/true);
  client_->SetDownloadSpeedResult(/*speed=*/123.45);
  auto result = RunNdtTestWithClient(mojom::NetworkBandwidthTestType::kDownload,
                                     std::move(client_));
  EXPECT_EQ(result, 123.45);
}

TEST_F(NdtClientTest, UploadTestPassed) {
  client_->SetTestResult(/*is_success=*/true);
  client_->SetUploadSpeedResult(/*speed=*/234.56);
  auto result = RunNdtTestWithClient(mojom::NetworkBandwidthTestType::kUpload,
                                     std::move(client_));
  EXPECT_EQ(result, 234.56);
}

TEST_F(NdtClientTest, TestFailed) {
  client_->SetTestResult(/*is_success=*/false);
  auto result = RunNdtTestWithClient(mojom::NetworkBandwidthTestType::kUpload,
                                     std::move(client_));
  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace diagnostics

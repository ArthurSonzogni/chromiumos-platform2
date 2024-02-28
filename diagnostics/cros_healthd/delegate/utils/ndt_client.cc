// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/ndt_client.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <libndt7/libndt7.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace libndt7 = ::measurementlab::libndt7;

class NdtClient final : public libndt7::Client {
 public:
  explicit NdtClient(
      libndt7::Settings settings,
      mojo::PendingRemote<mojom::NetworkBandwidthObserver> observer);
  NdtClient(const NdtClient&) = delete;
  NdtClient& operator=(const NdtClient&) = delete;
  ~NdtClient() override;

  // libndt7::Client overrides:
  void on_warning(const std::string& s) const override;
  void on_info(const std::string& s) const override;
  void on_performance(libndt7::NettestFlags tid,
                      uint8_t nflows,
                      uint64_t measured_bytes,
                      double elapsed_sec,
                      double max_runtime) override;

 private:
  // Observer to send the testing progress.
  mojo::Remote<mojom::NetworkBandwidthObserver> observer_;
};

NdtClient::NdtClient(
    libndt7::Settings settings,
    mojo::PendingRemote<mojom::NetworkBandwidthObserver> observer)
    : libndt7::Client(settings), observer_(std::move(observer)) {}

NdtClient::~NdtClient() = default;

void NdtClient::on_warning(const std::string& s) const {
  LOG(WARNING) << "NDT Warning: " << s;
}

void NdtClient::on_info(const std::string& s) const {
  LOG(INFO) << "NDT Info: " << s;
}

void NdtClient::on_performance(libndt7::NettestFlags tid,
                               uint8_t nflows,
                               uint64_t measured_bytes,
                               double elapsed_sec,
                               double max_runtime) {
  observer_->OnProgress(
      /*speed_kbps=*/libndt7::compute_speed_kbits(measured_bytes, elapsed_sec),
      /*percentage=*/elapsed_sec * 100.0 / max_runtime);
}

// Converts the test type in mojo to the nettest flag in libndt7.
libndt7::NettestFlags Convert(mojom::NetworkBandwidthTestType type) {
  switch (type) {
    case mojom::NetworkBandwidthTestType::kDownload:
      return libndt7::nettest_flag_download;
    case mojom::NetworkBandwidthTestType::kUpload:
      return libndt7::nettest_flag_upload;
  }
}

}  // namespace

std::optional<double> RunNdtTest(
    ash::cros_healthd::mojom::NetworkBandwidthTestType type,
    mojo::PendingRemote<ash::cros_healthd::mojom::NetworkBandwidthObserver>
        observer) {
  libndt7::Settings settings;
  settings.metadata["client_name"] = "cros_healthd";
  settings.metadata["client_version"] = "v0.1.0";
  settings.verbosity = libndt7::verbosity_info;
  settings.nettest_flags = Convert(type);
  auto client = std::make_unique<NdtClient>(settings, std::move(observer));
  return RunNdtTestWithClient(type, std::move(client));
}

std::optional<double> RunNdtTestWithClient(
    mojom::NetworkBandwidthTestType type,
    std::unique_ptr<measurementlab::libndt7::Client> client) {
  // Block until finish running.
  bool is_success = client->run();
  if (!is_success) {
    return std::nullopt;
  }

  auto summary = client->get_summary();
  switch (type) {
    case mojom::NetworkBandwidthTestType::kDownload:
      return summary.download_speed;
    case mojom::NetworkBandwidthTestType::kUpload:
      return summary.upload_speed;
  }
}

}  // namespace diagnostics

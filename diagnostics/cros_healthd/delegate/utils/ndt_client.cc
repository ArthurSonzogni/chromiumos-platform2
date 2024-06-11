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
#include <vboot/crossystem.h>

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

// Gets the user agent by OEM name for M-Lab. The user agent is used for
// capacity limiting of M-Lab services. To better understand traffic from
// production devices, normal and dev tags are included.
std::string ConstructUserAgent(const std::string& oem_name) {
  // If device is not in dev mode and not in debug build, add the normal tag.
  if (::VbGetSystemPropertyInt("devsw_boot") == 0 &&
      ::VbGetSystemPropertyInt("cros_debug") == 0) {
    return "cros_healthd-" + oem_name + "-normal/" + kNdtClientVersion;
  }
  return "cros_healthd-" + oem_name + "-dev/" + kNdtClientVersion;
}

}  // namespace

std::optional<double> RunNdtTest(
    ash::cros_healthd::mojom::NetworkBandwidthTestType type,
    const std::string& oem_name,
    mojo::PendingRemote<ash::cros_healthd::mojom::NetworkBandwidthObserver>
        observer) {
  libndt7::Settings settings;
  settings.metadata["client_name"] = "cros_healthd";
  settings.metadata["client_version"] = kNdtClientVersion;
  settings.user_agent = ConstructUserAgent(oem_name);
  settings.verbosity = libndt7::verbosity_info;
  settings.nettest_flags = Convert(type);
  settings.timeout = 10 /* seconds */;
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

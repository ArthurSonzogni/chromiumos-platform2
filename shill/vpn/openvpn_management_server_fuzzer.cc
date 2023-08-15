// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <memory>
#include <string_view>
#include <vector>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <net-base/socket.h>

#include "shill/net/io_handler.h"
#include "shill/vpn/openvpn_driver.h"
#include "shill/vpn/openvpn_management_server.h"

namespace shill {

class FakeOpenVPNDriver : public OpenVPNDriver {
 public:
  FakeOpenVPNDriver() : OpenVPNDriver(nullptr, nullptr) {}
  FakeOpenVPNDriver(const FakeOpenVPNDriver&) = delete;
  FakeOpenVPNDriver& operator=(const FakeOpenVPNDriver&) = delete;

  ~FakeOpenVPNDriver() override = default;

  void OnReconnecting(ReconnectReason) override {}
  void FailService(Service::ConnectFailure, std::string_view) override {}
  void ReportCipherMetrics(const std::string&) override {}
};

std::unique_ptr<net_base::Socket> CreateFakeSocket() {
  return net_base::Socket::CreateFromFd(
      base::ScopedFD(base::ScopedFD(open("/dev/null", O_RDONLY))));
}

class OpenVPNManagementServerFuzzer {
 public:
  void Run(const uint8_t* data, size_t size) {
    // First just send random strings.
    FuzzedDataProvider provider(data, size);
    OpenVPNManagementServer::ParseSubstring(
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeRandomLengthString(1024));

    // Next force some of the logic to actually run.
    OpenVPNManagementServer::ParseSubstring(
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeBytesAsString(1), provider.ConsumeBytesAsString(1));

    // Next the helpers.
    OpenVPNManagementServer::ParsePasswordTag(
        provider.ConsumeRandomLengthString(1024));
    OpenVPNManagementServer::ParsePasswordFailedReason(
        provider.ConsumeRandomLengthString(1024));

    // Send remaining data to test general entry point OnInput().
    auto data_vector = provider.ConsumeRemainingBytes<uint8_t>();
    InputData input_data(data_vector.data(), data_vector.size());
    FakeOpenVPNDriver driver;
    OpenVPNManagementServer server(&driver);
    server.connected_socket_ = CreateFakeSocket();
    server.socket_ = CreateFakeSocket();
    server.OnInput(&input_data);
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  OpenVPNManagementServerFuzzer fuzzer;
  fuzzer.Run(data, size);
  return 0;
}

}  // namespace shill

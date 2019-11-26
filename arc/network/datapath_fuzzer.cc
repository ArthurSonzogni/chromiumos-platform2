// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <net/if.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>

#include "arc/network/datapath.h"
#include "arc/network/minijailed_process_runner.h"
#include "arc/network/multicast_forwarder.h"
#include "arc/network/net_util.h"

namespace arc_networkd {

// Always succeeds
int ioctl_stub(int fd, unsigned long req, ...) {
  return 0;
}

class RandomProcessRunner : public MinijailedProcessRunner {
 public:
  explicit RandomProcessRunner(FuzzedDataProvider* data_provider)
      : data_provider_{data_provider} {}
  ~RandomProcessRunner() = default;

  int Run(const std::vector<std::string>& argv, bool log_failures) override {
    return data_provider_->ConsumeBool();
  }

  int AddInterfaceToContainer(const std::string& host_ifname,
                              const std::string& con_ifname,
                              const std::string& con_ipv4,
                              const std::string& con_nmask,
                              bool enable_multicast,
                              const std::string& con_pid) override {
    return data_provider_->ConsumeBool();
  }

 private:
  FuzzedDataProvider* data_provider_;

  DISALLOW_COPY_AND_ASSIGN(RandomProcessRunner);
};

namespace {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // <- DISABLE LOGGING.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOG_FATAL);

  FuzzedDataProvider provider(data, size);
  RandomProcessRunner runner(&provider);
  Datapath datapath(&runner, ioctl_stub);

  while (provider.remaining_bytes() > 0) {
    std::string ifname = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    std::string bridge = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    std::string addr =
        IPv4AddressToString(provider.ConsumeIntegral<uint32_t>());
    Subnet subnet(provider.ConsumeIntegral<int32_t>(),
                  provider.ConsumeIntegralInRange<int32_t>(0, 31),
                  base::Bind(&base::DoNothing));
    std::unique_ptr<SubnetAddress> subnet_addr = subnet.AllocateAtOffset(0);
    MacAddress mac;
    std::vector<uint8_t> bytes = provider.ConsumeBytes<uint8_t>(mac.size());
    std::copy(std::begin(bytes), std::begin(bytes), std::begin(mac));

    datapath.AddBridge(ifname, addr);
    datapath.RemoveBridge(ifname);
    datapath.AddInboundIPv4DNAT(ifname, addr);
    datapath.RemoveInboundIPv4DNAT(ifname, addr);
    datapath.AddVirtualBridgedInterface(ifname, MacAddressToString(mac),
                                        bridge);
    datapath.RemoveInterface(ifname);
    datapath.AddTAP(ifname, &mac, subnet_addr.get(), "");
    datapath.RemoveTAP(ifname);
  }

  return 0;
}

}  // namespace
}  // namespace arc_networkd

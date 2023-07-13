// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <net/if.h>

#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <dbus/message.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "patchpanel/dbus/client.h"
#include "patchpanel/dbus/mock_patchpanel_proxy.h"

namespace patchpanel {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
};

net_base::IPv4Address ConsumeIPv4Address(FuzzedDataProvider& provider) {
  const auto bytes =
      provider.ConsumeBytes<uint8_t>(net_base::IPv4Address::kAddressLength);
  return net_base::IPv4Address::CreateFromBytes(bytes).value_or(
      net_base::IPv4Address());
}

net_base::IPv4CIDR ConsumeIPv4CIDR(FuzzedDataProvider& provider) {
  const auto addr = ConsumeIPv4Address(provider);
  const int prefix_len = provider.ConsumeIntegralInRange(0, 32);
  return *net_base::IPv4CIDR::CreateFromAddressAndPrefix(addr, prefix_len);
}

net_base::IPv6Address ConsumeIPv6Address(FuzzedDataProvider& provider) {
  const auto bytes =
      provider.ConsumeBytes<uint8_t>(net_base::IPv6Address::kAddressLength);
  return net_base::IPv6Address::CreateFromBytes(bytes).value_or(
      net_base::IPv6Address());
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  dbus::Bus::Options options;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  auto client = Client::NewForTesting(
      bus, std::unique_ptr<org::chromium::PatchPanelProxyInterface>(
               new testing::NiceMock<MockPatchPanelProxy>()));
  FuzzedDataProvider provider(data, size);

  while (provider.remaining_bytes() > 0) {
    client->NotifyArcStartup(provider.ConsumeIntegral<pid_t>());
    client->NotifyArcVmStartup(provider.ConsumeIntegral<uint32_t>());
    client->NotifyArcVmShutdown(provider.ConsumeIntegral<uint32_t>());
    client->NotifyTerminaVmStartup(provider.ConsumeIntegral<uint32_t>());
    client->NotifyTerminaVmShutdown(provider.ConsumeIntegral<uint32_t>());
    client->NotifyParallelsVmStartup(provider.ConsumeIntegral<uint64_t>(),
                                     provider.ConsumeIntegral<int>());
    client->NotifyParallelsVmShutdown(provider.ConsumeIntegral<uint64_t>());
    // TODO(garrick): Enable the following once the memory leaks in Chrome OS
    // DBus are resolved.
    //    client->DefaultVpnRouting(provider.ConsumeIntegral<int>());
    //    client->RouteOnVpn(provider.ConsumeIntegral<int>());
    //    client->BypassVpn(provider.ConsumeIntegral<int>());
    client->ConnectNamespace(provider.ConsumeIntegral<pid_t>(),
                             provider.ConsumeRandomLengthString(100),
                             provider.ConsumeBool(), provider.ConsumeBool(),
                             Client::TrafficSource::kSystem);
    std::set<std::string> devices_for_counters;
    for (int i = 0; i < 10; i++) {
      if (provider.ConsumeBool()) {
        devices_for_counters.insert(
            provider.ConsumeRandomLengthString(IFNAMSIZ * 2));
      }
    }
    client->GetTrafficCounters(devices_for_counters, base::DoNothing());
  }
  bus->ShutdownAndBlock();
  return 0;
}

}  // namespace patchpanel

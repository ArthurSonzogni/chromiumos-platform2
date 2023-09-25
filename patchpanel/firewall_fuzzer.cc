// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <net/if.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <set>
#include <string>
#include <vector>

#include "base/logging.h"

#include "patchpanel/firewall.h"
#include "patchpanel/minijailed_process_runner.h"

using patchpanel::ModifyPortRuleRequest;
using Protocol = patchpanel::ModifyPortRuleRequest::Protocol;

namespace patchpanel {
namespace {

class FakeProcessRunner : public MinijailedProcessRunner {
 public:
  FakeProcessRunner() : MinijailedProcessRunner(nullptr, nullptr) {}
  FakeProcessRunner(const FakeProcessRunner&) = delete;
  FakeProcessRunner& operator=(const FakeProcessRunner&) = delete;
  ~FakeProcessRunner() = default;

  int RunIp(const std::vector<std::string>& argv,
            bool as_patchpanel_user,
            bool log_failures) override {
    return 0;
  }

  int RunIptables(std::string_view iptables_path,
                  Iptables::Table table,
                  Iptables::Command command,
                  std::string_view chain,
                  const std::vector<std::string>& argv,
                  bool log_failures,
                  std::optional<base::TimeDelta> timeout,
                  std::string* output) override {
    return 0;
  }

  int RunIpNetns(const std::vector<std::string>& argv,
                 bool log_failures) override {
    return 0;
  }
};
}  // namespace

}  // namespace patchpanel

net_base::IPv4Address ConsumeIPv4Address(FuzzedDataProvider& provider) {
  const auto bytes =
      provider.ConsumeBytes<uint8_t>(net_base::IPv4Address::kAddressLength);
  return net_base::IPv4Address::CreateFromBytes(bytes).value_or(
      net_base::IPv4Address());
}

struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

void FuzzAcceptRules(patchpanel::Firewall* firewall,
                     const uint8_t* data,
                     size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t port = data_provider.ConsumeIntegral<uint16_t>();
    std::string iface = data_provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    if (data_provider.ConsumeBool()) {
      firewall->AddAcceptRules(proto, port, iface);
    } else {
      firewall->DeleteAcceptRules(proto, port, iface);
    }
  }
}

void FuzzForwardRules(patchpanel::Firewall* firewall,
                      const uint8_t* data,
                      size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t forwarded_port = data_provider.ConsumeIntegral<uint16_t>();
    uint16_t dst_port = data_provider.ConsumeIntegral<uint16_t>();
    const auto input_ip = ConsumeIPv4Address(data_provider);
    const auto dst_ip = ConsumeIPv4Address(data_provider);
    std::string iface = data_provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    if (data_provider.ConsumeBool()) {
      firewall->AddIpv4ForwardRule(proto, input_ip, forwarded_port, iface,
                                   dst_ip, dst_port);
    } else {
      firewall->DeleteIpv4ForwardRule(proto, input_ip, forwarded_port, iface,
                                      dst_ip, dst_port);
    }
  }
}

void FuzzLoopbackLockdownRules(patchpanel::Firewall* firewall,
                               const uint8_t* data,
                               size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t port = data_provider.ConsumeIntegral<uint16_t>();
    if (data_provider.ConsumeBool()) {
      firewall->AddLoopbackLockdownRules(proto, port);
    } else {
      firewall->DeleteLoopbackLockdownRules(proto, port);
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  auto process_runner = new patchpanel::FakeProcessRunner();
  patchpanel::Firewall firewall(process_runner);

  FuzzAcceptRules(&firewall, data, size);
  FuzzForwardRules(&firewall, data, size);
  FuzzLoopbackLockdownRules(&firewall, data, size);

  return 0;
}

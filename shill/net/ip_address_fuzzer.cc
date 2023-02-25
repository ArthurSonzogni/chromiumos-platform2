// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netinet/in.h>
#include <sys/socket.h>

#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "shill/net/byte_string.h"
#include "shill/net/ip_address.h"

namespace shill {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  IPAddress::Family family = provider.ConsumeBool() ? AF_INET : AF_INET6;
  size_t prefixlen = provider.ConsumeIntegral<size_t>();
  std::vector<uint8_t> bytes = provider.ConsumeRemainingBytes<uint8_t>();
  ByteString bytestring(bytes);
  std::string str(bytes.begin(), bytes.end());

  std::string out;
  struct sockaddr_storage sockaddr;
  struct sockaddr* sockaddr_p = reinterpret_cast<struct sockaddr*>(&sockaddr);

  auto addr1 = IPAddress::CreateFromByteString(family, bytestring);
  if (addr1->IsValid()) {
    addr1->GetDefaultBroadcast();
    addr1->GetNetworkPart();
    addr1->IntoString(&out);
    addr1->IntoSockAddr(sockaddr_p, sizeof(sockaddr));
  }

  auto addr2 = IPAddress::CreateFromString(str);
  if (addr2->IsValid()) {
    addr2->GetDefaultBroadcast();
    addr2->GetNetworkPart();
    addr2->IntoString(&out);
    addr2->IntoSockAddr(sockaddr_p, sizeof(sockaddr));
  }

  auto addr3 = IPAddress::CreateFromPrefixString(str, family);
  if (addr3.has_value()) {
    addr3->GetDefaultBroadcast();
    addr3->GetNetworkPart();
    addr3->IntoString(&out);
    addr3->IntoSockAddr(sockaddr_p, sizeof(sockaddr));
  }

  IPAddress::GetPrefixLengthFromMask(IPAddress::kFamilyIPv4, str);
  IPAddress::GetAddressMaskFromPrefix(family, prefixlen);

  return 0;
}

}  // namespace shill

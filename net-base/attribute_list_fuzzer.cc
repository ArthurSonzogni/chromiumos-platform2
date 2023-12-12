// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "net-base/attribute_list.h"
#include "net-base/netlink_attribute.h"
#include "net-base/netlink_packet.h"

namespace net_base {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  const int log_level = provider.ConsumeIntegralInRange<int>(0, 8);
  const int indent = provider.ConsumeIntegralInRange<int>(0, 1024);
  const std::vector<uint8_t> payload =
      provider.ConsumeRemainingBytes<uint8_t>();
  net_base::NetlinkPacket packet(payload);
  if (!packet.IsValid()) {
    return 0;
  }

  AttributeListRefPtr attributes(new AttributeList);
  attributes->Decode(
      &packet,
      base::BindRepeating(&NetlinkAttribute::NewControlAttributeFromId));
  attributes->Encode();
  attributes->Print(log_level, indent);

  return 0;
}

}  // namespace net_base

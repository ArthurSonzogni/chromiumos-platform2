// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "shill/net/attribute_list.h"
#include "shill/net/netlink_attribute.h"

namespace shill {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  const size_t offset = provider.ConsumeIntegral<size_t>();
  const int log_level = provider.ConsumeIntegralInRange<int>(0, 8);
  const int indent = provider.ConsumeIntegralInRange<int>(0, 1024);
  const std::vector<uint8_t> payload =
      provider.ConsumeRemainingBytes<uint8_t>();

  AttributeListRefPtr attributes(new AttributeList);
  attributes->Decode(
      payload, offset,
      base::BindRepeating(&NetlinkAttribute::NewControlAttributeFromId));
  attributes->Encode();
  attributes->Print(log_level, indent);

  return 0;
}

}  // namespace shill

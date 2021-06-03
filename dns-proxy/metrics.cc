// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/metrics.h"

#include <map>
#include <type_traits>

#include <base/strings/string_util.h>

namespace dns_proxy {
namespace {

constexpr char kEventTemplate[] = "Network.DnsProxy.$1.Event";

const char* ProcessTypeString(Metrics::ProcessType type) {
  static const std::map<Metrics::ProcessType, const char*> m{
      {Metrics::ProcessType::kController, "Controller"},
      {Metrics::ProcessType::kProxySystem, "SystemProxy"},
      {Metrics::ProcessType::kProxyDefault, "DefaultProxy"},
      {Metrics::ProcessType::kProxyARC, "ARCProxy"},
  };
  const auto it = m.find(type);
  if (it != m.end())
    return it->second;

  return nullptr;
}

template <typename T>
constexpr auto value_of(T t) {
  return static_cast<std::underlying_type_t<T>>(t);
}

}  // namespace

void Metrics::RecordProcessEvent(Metrics::ProcessType type,
                                 Metrics::ProcessEvent event) {
  if (const char* ts = ProcessTypeString(type)) {
    const auto name =
        base::ReplaceStringPlaceholders(kEventTemplate, {ts}, nullptr);
    metrics_.SendEnumToUMA(name, value_of(event),
                           value_of(Metrics::ProcessEvent::kMaxValue));
    return;
  }

  LOG(DFATAL) << "Unknown type: " << value_of(type);
}

}  // namespace dns_proxy

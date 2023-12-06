// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/network_priority.h"

namespace net_base {

std::ostream& operator<<(std::ostream& stream,
                         const NetworkPriority& priority) {
  stream << "{";
  stream << priority.ranking_order;
  if (priority.is_primary_logical) {
    stream << ", primary_logical";
  }
  if (priority.is_primary_physical) {
    stream << ", primary_physical";
  }
  if (priority.is_primary_for_dns) {
    stream << ", primary_for_dns";
  }
  return stream << "}";
}
}  // namespace net_base

// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_NETIF_TOOL_H_
#define DEBUGD_SRC_NETIF_TOOL_H_

#include <string>

#include <base/macros.h>

namespace debugd {

class NetifTool {
 public:
  NetifTool() = default;
  NetifTool(const NetifTool&) = delete;
  NetifTool& operator=(const NetifTool&) = delete;

  ~NetifTool() = default;

  std::string GetInterfaces();
};

}  // namespace debugd

#endif  // DEBUGD_SRC_NETIF_TOOL_H_

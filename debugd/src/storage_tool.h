// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_STORAGE_TOOL_H_
#define DEBUGD_SRC_STORAGE_TOOL_H_

#include <string>

#include <base/macros.h>
#include <dbus-c++/dbus.h>

#include "debugd/src/subprocess_tool.h"

namespace debugd {

class StorageTool : public SubprocessTool {
 public:
  StorageTool() = default;
  ~StorageTool() override = default;

  std::string Smartctl(const std::string& option);
  std::string Start(const DBus::FileDescriptor& outfd);

 private:
  DISALLOW_COPY_AND_ASSIGN(StorageTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_STORAGE_TOOL_H_

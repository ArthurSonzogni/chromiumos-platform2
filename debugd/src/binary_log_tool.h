// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_BINARY_LOG_TOOL_H_
#define DEBUGD_SRC_BINARY_LOG_TOOL_H_

#include <map>
#include <string>

#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/debugd/dbus-constants.h>

namespace debugd {

class BinaryLogTool {
 public:
  void GetBinaryLogs(
      const std::string& username,
      const std::map<FeedbackBinaryLogType, base::ScopedFD>& outfds);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_BINARY_LOG_TOOL_H_

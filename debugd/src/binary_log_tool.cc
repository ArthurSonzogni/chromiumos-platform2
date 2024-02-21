// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/binary_log_tool.h"

#include <map>
#include <string>

#include <base/containers/span.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <base/logging.h>
#include <dbus/debugd/dbus-constants.h>

namespace debugd {

void BinaryLogTool::GetBinaryLogs(
    const std::string& username,
    const std::map<FeedbackBinaryLogType, base::ScopedFD>& outfds) {
  if (outfds.contains(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP)) {
    int out_fd = outfds.at(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP).get();

    // TODO(b/291347317): Placeholder code. Send dummy data for testing.
    // Implement binary log collection.
    constexpr std::string_view dummy_data = "test data";
    if (!base::WriteFileDescriptor(out_fd, dummy_data)) {
      PLOG(ERROR) << "Failed to send binary log";
      return;
    }
  } else {
    LOG(ERROR) << "Unsupported binary log type";
  }
}

}  // namespace debugd

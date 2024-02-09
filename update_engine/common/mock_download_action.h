// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_DOWNLOAD_ACTION_H_
#define UPDATE_ENGINE_COMMON_MOCK_DOWNLOAD_ACTION_H_

#include <stdint.h>

#include <gmock/gmock.h>

#include "update_engine/common/download_action.h"
#include "update_engine/common/error_code.h"

namespace chromeos_update_engine {

class MockDownloadActionDelegate : public DownloadActionDelegate {
 public:
  MOCK_METHOD3(BytesReceived,
               void(uint64_t bytes_progressed,
                    uint64_t bytes_received,
                    uint64_t total));
  MOCK_METHOD1(ShouldCancel, bool(ErrorCode* cancel_reason));
  MOCK_METHOD0(DownloadComplete, void());
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_DOWNLOAD_ACTION_H_

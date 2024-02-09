// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOLINTNEXTLINE(whitespace/line_length)
#ifndef UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_STATUS_UPDATE_HANDLER_H_
// NOLINTNEXTLINE(whitespace/line_length)
#define UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_STATUS_UPDATE_HANDLER_H_

#include <string>

#include "update_engine/client.h"
#include "update_engine/update_status.h"

namespace update_engine {

// Handles update_engine status changes. An instance of this class can be
// registered with UpdateEngineClient and will respond to any update_engine
// status changes.
class StatusUpdateHandler {
 public:
  virtual ~StatusUpdateHandler() = default;

  // Runs when we fail to register the handler due to an IPC error.
  virtual void IPCError(const std::string& error) = 0;

  // Runs every time update_engine reports a status change.
  virtual void HandleStatusUpdate(const UpdateEngineStatus& status) = 0;
};

}  // namespace update_engine

// NOLINTNEXTLINE(whitespace/line_length)
#endif  // UPDATE_ENGINE_CLIENT_LIBRARY_INCLUDE_UPDATE_ENGINE_STATUS_UPDATE_HANDLER_H_

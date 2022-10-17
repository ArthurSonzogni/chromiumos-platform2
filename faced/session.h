// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_SESSION_H_
#define FACED_SESSION_H_

#include <cstdint>

#include <absl/random/random.h>
#include <absl/status/status.h>
#include <base/callback_forward.h>

namespace faced {

// Generate a unique session ID.
//
// IDs should be used for debugging and diagnostics, and not security.
// We assume that the number of sessions during a single system boot is
// low enough that the probability of a collision is negligible.
uint64_t GenerateSessionId(absl::BitGen& bitgen);

// Interface for registering disconnect handler on a session.
class SessionInterface {
 public:
  virtual ~SessionInterface() = default;

  using StartCallback = base::OnceCallback<void(absl::Status)>;

  // Starts the session and invokes the callback with result status.
  virtual void Start(StartCallback callback) = 0;

  // Return a unique identifier for this session.
  //
  // The session id is used to identify a session across connections.
  // It is for debugging purposes only.
  virtual uint64_t session_id() = 0;

  using CompletionCallback = base::OnceCallback<void()>;

  // Register a callback to be called when the session is closed.
  //
  // It is invoked when the when the session ends and closes the connection.
  virtual void RegisterCompletionHandler(
      CompletionCallback completion_handler) = 0;
};

}  // namespace faced

#endif  // FACED_SESSION_H_

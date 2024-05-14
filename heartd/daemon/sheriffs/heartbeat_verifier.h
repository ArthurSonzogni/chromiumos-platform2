// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_SHERIFFS_HEARTBEAT_VERIFIER_H_
#define HEARTD_DAEMON_SHERIFFS_HEARTBEAT_VERIFIER_H_

#include "heartd/daemon/heartbeat_manager.h"
#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

class HeartbeatVerifier final : public Sheriff {
 public:
  explicit HeartbeatVerifier(HeartbeatManager* heartbeat_manager);
  HeartbeatVerifier(const HeartbeatVerifier&) = delete;
  HeartbeatVerifier& operator=(const HeartbeatVerifier&) = delete;
  ~HeartbeatVerifier() override = default;

  // heartd::Sheriff override:
  bool HasShiftWork() override;
  void AdjustSchedule() override;
  void ShiftWork() override;
  void CleanUp() override;

 private:
  // Unowned pointer. Should outlive this instance.
  HeartbeatManager* const heartbeat_manager_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_SHERIFFS_HEARTBEAT_VERIFIER_H_

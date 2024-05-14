// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_TOP_SHERIFF_H_
#define HEARTD_DAEMON_TOP_SHERIFF_H_

#include <memory>
#include <vector>

#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

class TopSheriff final : public Sheriff {
 public:
  explicit TopSheriff(base::OnceCallback<void()> quit_heartd_job);
  TopSheriff(const TopSheriff&) = delete;
  TopSheriff& operator=(const TopSheriff&) = delete;
  ~TopSheriff();

  // heartd::Sheriff override:
  void OneShotWork() override;
  bool HasShiftWork() override;
  void AdjustSchedule() override;
  void ShiftWork() override;
  void CleanUp() override;

  // Add managed sheriff.
  void AddSheriff(std::unique_ptr<Sheriff> sheriff);

 private:
  // Quit the heartd daemon.
  base::OnceCallback<void()> quit_heartd_job_;
  // Managed sheriffs.
  std::vector<std::unique_ptr<Sheriff>> sheriffs;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_TOP_SHERIFF_H_

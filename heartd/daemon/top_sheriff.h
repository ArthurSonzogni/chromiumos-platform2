// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_TOP_SHERIFF_H_
#define HEARTD_DAEMON_TOP_SHERIFF_H_

#include <memory>
#include <vector>

#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

class TopSheriff {
 public:
  TopSheriff();
  TopSheriff(const TopSheriff&) = delete;
  TopSheriff& operator=(const TopSheriff&) = delete;
  ~TopSheriff();

  // Ask managed sheriffs to start shift.
  void StartShift();

  // Returns if there are any active sheriffs.
  bool AnyActiveSheriff();

 private:
  // Managed sheriffs.
  std::vector<std::unique_ptr<Sheriff>> sheriffs;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_TOP_SHERIFF_H_

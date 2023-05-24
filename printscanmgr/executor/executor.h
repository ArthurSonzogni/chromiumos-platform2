// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTSCANMGR_EXECUTOR_EXECUTOR_H_
#define PRINTSCANMGR_EXECUTOR_EXECUTOR_H_

#include <brillo/daemons/daemon.h>

namespace printscanmgr {

// Daemon providing root-level privilege for printscanmgr.
class Executor final : public brillo::Daemon {
 public:
  Executor();
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor() override;
};

}  // namespace printscanmgr

#endif  // PRINTSCANMGR_EXECUTOR_EXECUTOR_H_

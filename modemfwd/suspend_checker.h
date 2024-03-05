// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_SUSPEND_CHECKER_H_
#define MODEMFWD_SUSPEND_CHECKER_H_

#include <memory>
#include <vector>

#include <base/files/file_path_watcher.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>

namespace modemfwd {

class SuspendChecker {
 public:
  static std::unique_ptr<SuspendChecker> Create();
  ~SuspendChecker();

  bool IsSuspendAnnounced() const;
  void RunWhenNotSuspending(base::OnceClosure cb);

 private:
  SuspendChecker();
  bool SetUpWatch();

  void OnWatcherEvent(const base::FilePath& path, bool error);
  void RunCallbacksIfSuspendNotAnnounced();

  std::unique_ptr<base::FilePathWatcher> suspend_announced_watcher_;
  std::vector<base::OnceClosure> callbacks_;

  base::WeakPtrFactory<SuspendChecker> weak_ptr_factory_{this};
};

}  // namespace modemfwd

#endif  // MODEMFWD_SUSPEND_CHECKER_H_

// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/calculator/stateful_free_space_calculator.h"

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/blkdev_utils/lvm.h>

namespace spaced {
namespace {

constexpr char kStatefulMountPath[] = "/mnt/stateful_partition";

}  // namespace

StatefulFreeSpaceCalculator::StatefulFreeSpaceCalculator(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    int64_t time_delta_seconds,
    std::optional<brillo::Thinpool> thinpool) {
  time_delta_seconds_ = time_delta_seconds;
  timer_.SetTaskRunner(task_runner);
  thinpool_ = thinpool;
  SetSize(-1);
}

void StatefulFreeSpaceCalculator::Start() {
  timer_.Start(FROM_HERE, base::Seconds(time_delta_seconds_), this,
               &StatefulFreeSpaceCalculator::UpdateSize);
}

void StatefulFreeSpaceCalculator::Stop() {
  timer_.AbandonAndStop();
}

void StatefulFreeSpaceCalculator::UpdateSize() {
  struct statvfs stat;

  if (StatVFS(base::FilePath(kStatefulMountPath), &stat) != 0) {
    LOG(ERROR) << "Failed to run statvfs() on stateful partition";
    SetSize(-1);
    return;
  }

  int64_t stateful_free_space =
      static_cast<int64_t>(stat.f_bavail) * stat.f_frsize;

  int64_t thinpool_free_space;
  if (thinpool_ && thinpool_->IsValid() &&
      thinpool_->GetFreeSpace(&thinpool_free_space)) {
    // There are two situations here that we need to account for:
    //
    // 1. First boot and post migration to LVM: the majority of the usage
    // resides on the stateful filesystem (and therefore the stateful
    // filesystem's free space is smaller).
    //
    // 2. The likelier scenario is in case there are other logical volumes
    // present; in this case, the amount of writes that can succeed on the
    // stateful filesystem are limited by the space available on the thinpool.
    stateful_free_space = std::min(stateful_free_space, thinpool_free_space);
  }

  SetSize(stateful_free_space);
}

int StatefulFreeSpaceCalculator::StatVFS(const base::FilePath& path,
                                         struct statvfs* st) {
  return statvfs(path.value().c_str(), st);
}

}  // namespace spaced

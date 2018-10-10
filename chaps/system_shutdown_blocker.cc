// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/system_shutdown_blocker.h"

#include <string>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/strings/stringprintf.h>

namespace chaps {

SystemShutdownBlocker::SystemShutdownBlocker(
    const scoped_refptr<base::SingleThreadTaskRunner>&
        origin_thread_task_runner)
    : origin_thread_task_runner_(origin_thread_task_runner) {}

SystemShutdownBlocker::~SystemShutdownBlocker() {
  for (int slot_id : blocked_slots_)
    PerformUnblock(slot_id);
}

void SystemShutdownBlocker::Block(int slot_id,
                                  base::TimeDelta fallback_timeout) {
  // Post block task.
  auto perform_block_task = base::Bind(&SystemShutdownBlocker::PerformBlock,
                                       base::Unretained(this),
                                       slot_id);
  origin_thread_task_runner_->PostTask(FROM_HERE, perform_block_task);

  // Post delayed unblock task (fallback).
  auto perform_unblock_task = base::Bind(&SystemShutdownBlocker::PerformUnblock,
                                         base::Unretained(this),
                                         slot_id);
  origin_thread_task_runner_->PostDelayedTask(FROM_HERE, perform_unblock_task,
                                              fallback_timeout);
}

void SystemShutdownBlocker::Unblock(int slot_id) {
  // Post unblock task.
  auto perform_unblock_task = base::Bind(&SystemShutdownBlocker::PerformUnblock,
                                         base::Unretained(this),
                                         slot_id);
  origin_thread_task_runner_->PostTask(FROM_HERE, perform_unblock_task);
}

void SystemShutdownBlocker::PerformBlock(int slot_id) {
  // Create lock file with chapsd PID as content and readable for powerd.
  const base::FilePath lock_path = GetPowerdLockFilePath(slot_id);
  if (!base::DirectoryExists(lock_path.DirName())) {
    LOG(ERROR) << "Failed to create lock file (" << lock_path.DirName().value()
               << " doesn't exist)";
    return;
  }
  blocked_slots_.insert(slot_id);

  base::File lock_file(lock_path,
                       base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  std::string lock_contents = base::StringPrintf("%d", getpid());
  int mode = base::FILE_PERMISSION_READ_BY_USER |
             base::FILE_PERMISSION_WRITE_BY_USER |
             base::FILE_PERMISSION_READ_BY_GROUP |
             base::FILE_PERMISSION_READ_BY_OTHERS;  // chmod 644
  if (!lock_file.IsValid() ||
      lock_file.WriteAtCurrentPos(lock_contents.data(), lock_contents.size()) <
          0 ||
      !base::SetPosixFilePermissions(lock_path, mode)) {
    PLOG(ERROR) << "Failed to create lock file.";
    return;
  }
  LOG(INFO) << "Created lock file: " << lock_path.value();
}

void SystemShutdownBlocker::PerformUnblock(int slot_id) {
  if (blocked_slots_.find(slot_id) == blocked_slots_.end())
    return;

  const base::FilePath lock_path = GetPowerdLockFilePath(slot_id);
  if (!base::PathExists(lock_path)) {
    LOG(WARNING) << "Couldn't delete lock file (not existant): "
                 << lock_path.value();
    return;
  }
  if (!base::DeleteFile(lock_path, false /* recursive */)) {
    PLOG(ERROR) << "Couldn't delete lock file: " << lock_path.value();
    return;
  }
  blocked_slots_.erase(slot_id);
  LOG(INFO) << "Deleted lock file: " << lock_path.value();
}

base::FilePath SystemShutdownBlocker::GetPowerdLockFilePath(int slot_id) const {
  return base::FilePath("/run/lock/power_override/chapsd_token_init_slot_" +
                        std::to_string(slot_id) + ".lock");
}

}  // namespace chaps

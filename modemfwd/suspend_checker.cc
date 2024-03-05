// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/suspend_checker.h"

#include <utility>

#include <base/files/file_util.h>

#include "modemfwd/logging.h"

namespace {
constexpr char kSuspendAnnouncedFile[] =
    "/run/power_manager/power/suspend_announced";
}  // namespace

namespace modemfwd {

SuspendChecker::SuspendChecker() = default;

// static
std::unique_ptr<SuspendChecker> SuspendChecker::Create() {
  std::unique_ptr<SuspendChecker> checker =
      std::unique_ptr<SuspendChecker>(new SuspendChecker);
  if (!checker->SetUpWatch()) {
    LOG(ERROR) << "Could not set up suspend announce file watch";
    return nullptr;
  }
  return checker;
}

SuspendChecker::~SuspendChecker() = default;

bool SuspendChecker::IsSuspendAnnounced() const {
  return base::PathExists(base::FilePath(kSuspendAnnouncedFile));
}

void SuspendChecker::RunWhenNotSuspending(base::OnceClosure cb) {
  callbacks_.push_back(std::move(cb));

  // Run callbacks inline if suspend has not been announced.
  RunCallbacksIfSuspendNotAnnounced();
}

bool SuspendChecker::SetUpWatch() {
  suspend_announced_watcher_.reset(new base::FilePathWatcher);
  return suspend_announced_watcher_->Watch(
      base::FilePath(kSuspendAnnouncedFile),
      base::FilePathWatcher::Type::kNonRecursive,
      base::BindRepeating(&SuspendChecker::OnWatcherEvent,
                          weak_ptr_factory_.GetWeakPtr()));
}

void SuspendChecker::OnWatcherEvent(const base::FilePath& /* path */,
                                    bool error) {
  if (error) {
    LOG(WARNING) << "Suspend announcement watch returned an error. "
                 << "Attempting to reset watch";
    if (!SetUpWatch()) {
      LOG(ERROR) << "Could not reset suspend announcement watch";
    }
  }

  // We might be notified for file creation, etc. so we have to check for
  // the existence of the file anyway. The documentation for FilePathWatcher
  // says we can get more information by using ChangeInfo, but this is stated
  // to only be a "strong hint", so there are no guarantees we can always use
  // that to immediately know the file has been deleted.
  RunCallbacksIfSuspendNotAnnounced();
}

void SuspendChecker::RunCallbacksIfSuspendNotAnnounced() {
  if (IsSuspendAnnounced()) {
    ELOG(INFO) << "Suspend has been announced, deferring tasks";
    return;
  }

  EVLOG(1) << "Not currently suspending, running tasks";
  for (auto& callback : callbacks_) {
    std::move(callback).Run();
  }
  callbacks_.clear();
}

}  // namespace modemfwd

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/fanotify_watcher.h"

#include <fcntl.h>
#include <sys/fanotify.h>
#include <sys/utsname.h>
#include <utility>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/posix/eintr_wrapper.h"
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_task_runner_handle.h"
#include "dlp/fanotify_reader_thread.h"

namespace dlp {

namespace {

// Returns the current kernel version. If there is a failure to retrieve the
// version, it returns <INT_MIN, INT_MIN>.
std::pair<int, int> GetKernelVersion() {
  struct utsname buf;
  if (uname(&buf))
    return std::make_pair(INT_MIN, INT_MIN);

  // Parse uname result in the form of x.yy.zzz. The parsed data should be in
  // the expected format.
  std::vector<base::StringPiece> versions = base::SplitStringPiece(
      buf.release, ".", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);
  DCHECK_EQ(versions.size(), 3);
  DCHECK(!versions[0].empty());
  DCHECK(!versions[1].empty());
  int version;
  bool result = base::StringToInt(versions[0], &version);
  DCHECK(result);
  int major_revision;
  result = base::StringToInt(versions[1], &major_revision);
  DCHECK(result);
  return std::make_pair(version, major_revision);
}

constexpr std::pair<int, int> kMinKernelVersionForFanDeleteEvents =
    std::make_pair(5, 1);

}  // namespace

FanotifyWatcher::FanotifyWatcher(Delegate* delegate)
    : task_runner_(base::SequencedTaskRunnerHandle::Get()),
      fd_events_thread_(task_runner_, this),
      fh_events_thread_(task_runner_, this),
      delegate_(delegate) {
  DCHECK(delegate);
  fanotify_fd_events_fd_.reset(fanotify_init(
      FAN_CLOEXEC | FAN_CLASS_CONTENT, O_RDONLY | O_CLOEXEC | O_LARGEFILE));
  PCHECK(fanotify_fd_events_fd_.is_valid())
      << "fanotify_init() for permission events failed";
  fd_events_thread_.StartThread(fanotify_fd_events_fd_.get());

  if (GetKernelVersion() >= kMinKernelVersionForFanDeleteEvents) {
    fanotify_fh_events_fd_.reset(
        fanotify_init(FAN_CLASS_NOTIF | /*FAN_REPORT_FID=*/0x00000200, 0));
    PCHECK(fanotify_fh_events_fd_.is_valid())
        << "fanotify_init() for notification events failed";
    fh_events_thread_.StartThread(fanotify_fh_events_fd_.get());
  }
}

FanotifyWatcher::~FanotifyWatcher() = default;

void FanotifyWatcher::AddWatch(const base::FilePath& path) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // We need to mark the whole filesystem in order to receive events from all
  // the mounts with the protected files and from all mount namespaces.
  // FAN_MARK_FILESYSTEM is available only since Linux 4.20 and the following
  // call will fail on boards with older kernels.
  int res = fanotify_mark(fanotify_fd_events_fd_.get(),
                          FAN_MARK_ADD | FAN_MARK_FILESYSTEM, FAN_OPEN_PERM,
                          AT_FDCWD, path.value().c_str());

  if (res != 0) {
    PLOG(ERROR) << "fanotify_mark for OPEN_PERM (" << path << ") failed";
  }
}

void FanotifyWatcher::AddFileDeleteWatch(const base::FilePath& path) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (GetKernelVersion() >= kMinKernelVersionForFanDeleteEvents) {
    int res = fanotify_mark(fanotify_fh_events_fd_.get(), FAN_MARK_ADD,
                            FAN_DELETE_SELF, AT_FDCWD, path.value().c_str());

    if (res != 0) {
      PLOG(ERROR) << "fanotify_mark for DELETE_SELF (" << path << ") failed";
    } else {
      LOG(INFO) << "Added watch for: " << path;
    }
  }
}

void FanotifyWatcher::OnFileOpenRequested(ino_t inode,
                                          int pid,
                                          base::ScopedFD fd) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK(fanotify_fd_events_fd_.is_valid());

  delegate_->ProcessFileOpenRequest(
      inode, pid,
      base::BindOnce(&FanotifyWatcher::OnRequestProcessed,
                     base::Unretained(this), std::move(fd)));
}

void FanotifyWatcher::OnFileDeleted(ino_t inode) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  delegate_->OnFileDeleted(inode);
}

void FanotifyWatcher::OnRequestProcessed(base::ScopedFD fd, bool allowed) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  struct fanotify_response response = {};
  response.fd = fd.get();
  response.response = allowed ? FAN_ALLOW : FAN_DENY;
  HANDLE_EINTR(
      write(fanotify_fd_events_fd_.get(), &response, sizeof(response)));
}

}  // namespace dlp

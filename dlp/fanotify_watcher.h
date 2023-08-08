// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_FANOTIFY_WATCHER_H_
#define DLP_FANOTIFY_WATCHER_H_

#include <map>
#include <memory>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "dlp/dlp_metrics.h"
#include "dlp/fanotify_reader_thread.h"
#include "dlp/file_id.h"

namespace dlp {

// Interacts with fanotify API to process file access events.
// Starts to listen to the events immediately on both file descriptors,
// but allows all OPEN_PERM requests unless |active_| is being set.
class FanotifyWatcher : public FanotifyReaderThread::Delegate {
 public:
  class Delegate {
   public:
    virtual void ProcessFileOpenRequest(
        FileId id, int pid, base::OnceCallback<void(bool)> callback) = 0;

    virtual void OnFileDeleted(FileId id) = 0;

    virtual void OnFanotifyError(FanotifyError error) = 0;
  };

  FanotifyWatcher(Delegate* delegate,
                  int fanotify_perm_fd,
                  int fanotify_notif_fd);
  ~FanotifyWatcher() override;
  FanotifyWatcher(const FanotifyWatcher&) = delete;
  FanotifyWatcher& operator=(const FanotifyWatcher&) = delete;

  // Start to listen to DELETE_SELF event for the file on |path|.
  void AddFileDeleteWatch(const base::FilePath& path);

  // If |active| is true, starts processing of OPEN_PERM requests, otherwise
  // sets to always allow them.
  void SetActive(bool active);
  bool IsActive() const;

 private:
  // FanotifyReaderThread::Delegate overrides:
  void OnFileOpenRequested(
      ino_t inode,
      int pid,
      base::ScopedFD fd,
      std::unique_ptr<FanotifyReaderThread::FanotifyReplyWatchdog> watchdog)
      override;
  void OnFileDeleted(ino_t inode) override;
  void OnFanotifyError(FanotifyError error) override;

  void OnRequestProcessed(
      base::ScopedFD fd,
      std::unique_ptr<FanotifyReaderThread::FanotifyReplyWatchdog> watchdog,
      bool allowed);

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  bool active_ = false;

  // We need two sets of fanotify file descriptors and thread so that one of
  // them identifies objects by file handles (FAN_CLASS_NOTIF) and another
  // identifies objects by file descriptors (FAN_CLASS_CONTENT).
  //
  // fanotify file descriptors should be destructed before the reader thread so
  // that the read loop there will exit on closed file descriptor.
  FanotifyReaderThread fd_events_thread_;
  FanotifyReaderThread fh_events_thread_;
  base::ScopedFD fanotify_fd_events_fd_;
  base::ScopedFD fanotify_fh_events_fd_;

  Delegate* delegate_;
};

}  // namespace dlp

#endif  // DLP_FANOTIFY_WATCHER_H_

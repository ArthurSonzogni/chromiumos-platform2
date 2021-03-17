// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_FANOTIFY_WATCHER_H_
#define DLP_FANOTIFY_WATCHER_H_

#include "dlp/fanotify_reader_thread.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/memory/scoped_refptr.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace dlp {

// Interacts with fanotify API to process file access events.
class FanotifyWatcher : public FanotifyReaderThread::Delegate {
 public:
  class Delegate {
   public:
    virtual bool ProcessFileOpenRequest(ino_t inode, int pid) = 0;
  };

  explicit FanotifyWatcher(Delegate* delegate);
  ~FanotifyWatcher();
  FanotifyWatcher(const FanotifyWatcher&) = delete;
  FanotifyWatcher& operator=(const FanotifyWatcher&) = delete;

  // Start to listen to event for the mount point with |path|.
  void AddWatch(const base::FilePath& path);

 private:
  void OnFileOpenRequested(ino_t inode, int pid, base::ScopedFD fd) override;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  // fanotify file descriptor should be destructed before the reader thread so
  // that the read loop there will exit on closed file descriptor.
  FanotifyReaderThread thread_;
  base::ScopedFD fanotify_fd_;
  Delegate* delegate_;
};

}  // namespace dlp

#endif  // DLP_FANOTIFY_WATCHER_H_

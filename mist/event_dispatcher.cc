// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mist/event_dispatcher.h"

#include <base/location.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>

using base::MessageLoop;
using base::MessageLoopForIO;
using base::StringPrintf;
using std::unique_ptr;

namespace mist {

EventDispatcher::EventDispatcher()
    : dont_use_directly_(new MessageLoopForIO()),
      task_runner_(base::ThreadTaskRunnerHandle::Get()) {
  CHECK(dont_use_directly_);
}

EventDispatcher::~EventDispatcher() {
  StopWatchingAllFileDescriptors();
}

void EventDispatcher::DispatchForever() {
  MessageLoop::current()->Run();
}

void EventDispatcher::Stop() {
  MessageLoop::current()->PostTask(FROM_HERE,
                                   MessageLoop::QuitWhenIdleClosure());
}

bool EventDispatcher::PostTask(const base::Closure& task) {
  return task_runner_->PostTask(FROM_HERE, task);
}

bool EventDispatcher::PostDelayedTask(const base::Closure& task,
                                      const base::TimeDelta& delay) {
  return task_runner_->PostDelayedTask(FROM_HERE, task, delay);
}

bool EventDispatcher::StartWatchingFileDescriptor(
    int file_descriptor,
    MessageLoopForIO::Mode mode,
    MessageLoopForIO::Watcher* watcher) {
  CHECK_GE(file_descriptor, 0);
  CHECK(watcher);

  unique_ptr<MessageLoopForIO::FileDescriptorWatcher>
      scoped_file_descriptor_watcher;
  MessageLoopForIO::FileDescriptorWatcher* file_descriptor_watcher;

  FileDescriptorWatcherMap::iterator it =
      file_descriptor_watchers_.find(file_descriptor);
  if (it == file_descriptor_watchers_.end()) {
    file_descriptor_watcher = new MessageLoopForIO::FileDescriptorWatcher();
    CHECK(file_descriptor_watcher);
    scoped_file_descriptor_watcher.reset(file_descriptor_watcher);
  } else {
    // MessageLoopForIO::WatchFileDescriptor supports watching the same file
    // descriptor again, using the same mode or a different one.
    file_descriptor_watcher = it->second;
  }

  if (!MessageLoopForIO::current()->WatchFileDescriptor(
          file_descriptor, true, mode, file_descriptor_watcher, watcher)) {
    LOG(ERROR) << StringPrintf("Could not watch file descriptor %d.",
                               file_descriptor);
    return false;
  }

  if (scoped_file_descriptor_watcher) {
    file_descriptor_watchers_[file_descriptor] =
        scoped_file_descriptor_watcher.release();
  }
  VLOG(2) << StringPrintf("Started watching file descriptor %d.",
                          file_descriptor);
  return true;
}

bool EventDispatcher::StopWatchingFileDescriptor(int file_descriptor) {
  CHECK_GE(file_descriptor, 0);

  FileDescriptorWatcherMap::iterator it =
      file_descriptor_watchers_.find(file_descriptor);
  if (it == file_descriptor_watchers_.end()) {
    LOG(ERROR) << StringPrintf("File descriptor %d is not being watched.",
                               file_descriptor);
    return false;
  }

  MessageLoopForIO::FileDescriptorWatcher* file_descriptor_watcher = it->second;
  delete file_descriptor_watcher;
  file_descriptor_watchers_.erase(it);
  VLOG(2) << StringPrintf("Stopped watching file descriptor %d.",
                          file_descriptor);
  return true;
}

void EventDispatcher::StopWatchingAllFileDescriptors() {
  while (!file_descriptor_watchers_.empty())
    CHECK(StopWatchingFileDescriptor(file_descriptor_watchers_.begin()->first));
}

}  // namespace mist

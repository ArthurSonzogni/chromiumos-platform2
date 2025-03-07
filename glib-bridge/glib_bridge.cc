// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "glib-bridge/glib_bridge.h"

#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/task/sequenced_task_runner.h>

namespace glib_bridge {

namespace {

struct GMainContextLock {
 public:
  explicit GMainContextLock(GMainContext* context) : context_(context) {
    CHECK(context_);
    CHECK(g_main_context_acquire(context_));
  }

  ~GMainContextLock() { g_main_context_release(context_); }

 private:
  GMainContext* context_;  // weak
};

}  // namespace

GlibBridge::GlibBridge()
    : glib_context_(g_main_context_new()),
      state_(State::kPreparingIteration),
      weak_ptr_factory_(this) {
  CHECK(glib_context_);
  g_main_context_push_thread_default(glib_context_);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&GlibBridge::PrepareIteration,
                                weak_ptr_factory_.GetWeakPtr()));
}

GlibBridge::~GlibBridge() {
  g_main_context_pop_thread_default(glib_context_);
  g_main_context_unref(glib_context_);
}

void GlibBridge::PrepareIteration() {
  CHECK_EQ(state_, State::kPreparingIteration);
  CHECK(watchers_.empty());
  GMainContextLock _l(glib_context_);

  bool immediate = g_main_context_prepare(glib_context_, &max_priority_);

  int num_fds =
      g_main_context_query(glib_context_, max_priority_, nullptr, nullptr, 0);
  poll_fds_ = std::vector<GPollFD>(num_fds);

  int timeout_ms;
  g_main_context_query(glib_context_, max_priority_, &timeout_ms, &poll_fds_[0],
                       num_fds);
  if (immediate || (num_fds == 0 && timeout_ms == 0)) {
    DVLOG(1) << "Iteration can be dispatched immediately";
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&GlibBridge::Dispatch, weak_ptr_factory_.GetWeakPtr()));
    state_ = State::kReadyForDispatch;
    return;
  }

  // Collect information about which poll flags we need for each fd.
  std::map<int, int> poll_flags;
  for (GPollFD& poll_fd : poll_fds_) {
    fd_map_[poll_fd.fd].push_back(&poll_fd);
    poll_flags[poll_fd.fd] |= poll_fd.events;
  }

  DVLOG(1) << "Preparing iteration with timeout " << timeout_ms << " ms, "
           << poll_flags.size() << " event FDs";

  for (const auto& fd_flags : poll_flags) {
    // Duplicate fds glib wants to watch so glib closing them won't crash our
    // message loop stack. See b/349247466
    base::ScopedFD watch_fd(dup(fd_flags.first));
    int bare_fd = watch_fd.get();
    PCHECK(bare_fd >= 0) << "Could not duplicate glib fd";

    std::unique_ptr<base::FileDescriptorWatcher::Controller> reader;
    if (fd_flags.second & G_IO_IN) {
      reader = base::FileDescriptorWatcher::WatchReadable(
          bare_fd, base::BindRepeating(&GlibBridge::OnEvent,
                                       weak_ptr_factory_.GetWeakPtr(), bare_fd,
                                       G_IO_IN));
      CHECK(reader) << "Could not set up read watcher for fd " << bare_fd;
    }

    std::unique_ptr<base::FileDescriptorWatcher::Controller> writer;
    if (fd_flags.second & G_IO_OUT) {
      writer = base::FileDescriptorWatcher::WatchWritable(
          bare_fd, base::BindRepeating(&GlibBridge::OnEvent,
                                       weak_ptr_factory_.GetWeakPtr(), bare_fd,
                                       G_IO_OUT));
      CHECK(writer) << "Could not set up write watcher for fd " << bare_fd;
    }

    watchers_[fd_flags.first] =
        Watcher{std::move(watch_fd), std::move(reader), std::move(writer)};
  }

  state_ = State::kWaitingForEvents;
  if (timeout_ms < 0) {
    return;
  }

  base::TimeDelta timeout = base::Milliseconds(timeout_ms);
  timeout_closure_.Reset(
      base::BindOnce(&GlibBridge::Timeout, weak_ptr_factory_.GetWeakPtr()));
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, timeout_closure_.callback(), timeout);
}

void GlibBridge::OnEvent(int fd, int flag) {
  CHECK(state_ == State::kWaitingForEvents ||
        state_ == State::kReadyForDispatch);
  int glib_fd = WatchFdToGlibFd(fd);
  CHECK_GE(glib_fd, 0);
  DVLOG(2) << "OnEvent(" << fd << " [" << glib_fd << "], " << flag << ")";

  for (GPollFD* poll_fd : fd_map_[glib_fd]) {
    poll_fd->revents |= flag & poll_fd->events;
  }

  if (flag & G_IO_IN) {
    watchers_[glib_fd].reader.reset();
  }
  if (flag & G_IO_OUT) {
    watchers_[glib_fd].writer.reset();
  }

  // Avoid posting the dispatch task if it's already posted
  if (state_ == State::kReadyForDispatch) {
    DVLOG(2) << "Dispatch was already scheduled";
    return;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&GlibBridge::Dispatch, weak_ptr_factory_.GetWeakPtr()));
  state_ = State::kReadyForDispatch;
}

void GlibBridge::Timeout() {
  if (state_ == State::kWaitingForEvents) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&GlibBridge::Dispatch, weak_ptr_factory_.GetWeakPtr()));
    state_ = State::kReadyForDispatch;
  } else if (state_ == State::kReadyForDispatch) {
    DVLOG(2) << "Dispatch was already scheduled, ignoring timeout";
  } else {
    LOG(FATAL) << "Unexpected state "
               << static_cast<std::underlying_type_t<State>>(state_)
               << " in timeout handler";
  }
}

void GlibBridge::Dispatch() {
  CHECK_EQ(state_, State::kReadyForDispatch);
  GMainContextLock _l(glib_context_);

  bool dispatched = g_main_context_check(glib_context_, max_priority_,
                                         poll_fds_.data(), poll_fds_.size());
  g_main_context_dispatch(glib_context_);
  DVLOG(2) << (dispatched ? "Found" : "Did not find") << " source to dispatch";

  timeout_closure_.Cancel();
  watchers_.clear();
  poll_fds_.clear();
  fd_map_.clear();
  max_priority_ = -1;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&GlibBridge::PrepareIteration,
                                weak_ptr_factory_.GetWeakPtr()));
  state_ = State::kPreparingIteration;
}

int GlibBridge::WatchFdToGlibFd(int fd) {
  for (const auto& watcher : watchers_) {
    if (watcher.second.watch_fd == fd) {
      return watcher.first;
    }
  }
  return -1;
}

}  // namespace glib_bridge
